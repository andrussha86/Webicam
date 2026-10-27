import CoreImage
import CoreImage.CIFilterBuiltins
import QuartzCore
import Vision
import WebicamCore

/// Puts something else behind the person: their own background blurred, a picture from the PC or
/// a flat colour. Vision person segmentation (Neural Engine) gives the mask, Core Image blends,
/// the result is rendered into an NV12 buffer for the encoder. Used only on the capture queue.
final class BackgroundCompositor {
    /// Called once per picture that cannot be shown (undecodable, or image mode with none received).
    var onImageError: (() -> Void)?

    private let context = CIContext(options: [.cacheIntermediates: false])
    private let request: VNGeneratePersonSegmentationRequest = {
        let request = VNGeneratePersonSegmentationRequest()
        request.qualityLevel = .balanced
        request.outputPixelFormat = kCVPixelFormatType_OneComponent8
        return request
    }()
    private var mask: CIImage?
    private var maskTime: CFTimeInterval = 0
    private var pool: CVPixelBufferPool?
    private var poolSize = (0, 0)
    private var imageData: Data?
    private var picture: (image: CIImage, width: Int, height: Int)?
    private var imageErrorReported = false

    /// The mask is recomputed at most this often; at 60 fps every other frame reuses it.
    private static let maskInterval: CFTimeInterval = 1.0 / 30
    /// Gaussian sigma at strength 100 for a 1080-line frame.
    private static let maxSigma = 40.0

    /// Drops the cached mask (lens or mode changed), so a stale one never shows up.
    func reset() {
        mask = nil
    }

    /// The PC's picture for image mode; replaces the previous one.
    func setImage(_ jpeg: Data) {
        imageData = jpeg.isEmpty ? nil : jpeg
        picture = nil
        imageErrorReported = false
    }

    /// A new buffer with the background replaced, or nil to send the original.
    func process(_ pixelBuffer: CVPixelBuffer, settings: CameraSettings) -> CVPixelBuffer? {
        guard settings.backgroundMode != .off else { return nil }
        let width = CVPixelBufferGetWidth(pixelBuffer)
        let height = CVPixelBufferGetHeight(pixelBuffer)
        let image = CIImage(cvPixelBuffer: pixelBuffer)
        guard let background = background(for: image, settings: settings, width: width, height: height) else {
            return nil
        }

        let now = CACurrentMediaTime()
        if mask == nil || now - maskTime >= Self.maskInterval {
            let handler = VNImageRequestHandler(cvPixelBuffer: pixelBuffer, orientation: .up)
            if (try? handler.perform([request])) != nil, let result = request.results?.first {
                mask = CIImage(cvPixelBuffer: result.pixelBuffer)
                maskTime = now
            }
        }
        guard let mask, let output = makeBuffer(width: width, height: height) else { return nil }

        let scaledMask = mask.transformed(by: CGAffineTransform(scaleX: CGFloat(width) / mask.extent.width,
                                                                y: CGFloat(height) / mask.extent.height))
        let blend = CIFilter.blendWithMask()
        blend.inputImage = image
        blend.backgroundImage = background
        blend.maskImage = scaledMask
        guard let result = blend.outputImage else { return nil }
        context.render(result, to: output)
        return output
    }

    private func background(for image: CIImage, settings: CameraSettings, width: Int, height: Int) -> CIImage? {
        switch settings.backgroundMode {
        case .off:
            return nil
        case .blur:
            let sigma = Double(max(settings.blur, 1)) / 100 * Self.maxSigma * Double(height) / 1080
            return image.clampedToExtent().applyingGaussianBlur(sigma: sigma).cropped(to: image.extent)
        case .color:
            let rgb = settings.backgroundColor
            let color = CIColor(red: CGFloat((rgb >> 16) & 0xFF) / 255, green: CGFloat((rgb >> 8) & 0xFF) / 255,
                                blue: CGFloat(rgb & 0xFF) / 255)
            return CIImage(color: color).cropped(to: image.extent)
        case .image:
            if let picture, picture.width == width, picture.height == height { return picture.image }
            guard let imageData, let decoded = CIImage(data: imageData), !decoded.extent.isEmpty else {
                if !imageErrorReported {
                    imageErrorReported = true
                    onImageError?()
                }
                return nil
            }
            // Cover: scale to fill the frame, centre, crop the overflow.
            let scale = max(CGFloat(width) / decoded.extent.width, CGFloat(height) / decoded.extent.height)
            let scaled = decoded.transformed(by: CGAffineTransform(scaleX: scale, y: scale))
            let dx = (CGFloat(width) - scaled.extent.width) / 2 - scaled.extent.minX
            let dy = (CGFloat(height) - scaled.extent.height) / 2 - scaled.extent.minY
            let fitted = scaled.transformed(by: CGAffineTransform(translationX: dx, y: dy))
                .cropped(to: CGRect(x: 0, y: 0, width: width, height: height))
                .insertingIntermediate(cache: true)
            picture = (fitted, width, height)
            return fitted
        }
    }

    private func makeBuffer(width: Int, height: Int) -> CVPixelBuffer? {
        if pool == nil || poolSize != (width, height) {
            let attributes: [String: Any] = [
                kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                kCVPixelBufferWidthKey as String: width,
                kCVPixelBufferHeightKey as String: height,
                kCVPixelBufferIOSurfacePropertiesKey as String: [String: Any](),
            ]
            var newPool: CVPixelBufferPool?
            CVPixelBufferPoolCreate(nil, nil, attributes as CFDictionary, &newPool)
            pool = newPool
            poolSize = (width, height)
        }
        guard let pool else { return nil }
        var buffer: CVPixelBuffer?
        CVPixelBufferPoolCreatePixelBuffer(nil, pool, &buffer)
        return buffer
    }
}
