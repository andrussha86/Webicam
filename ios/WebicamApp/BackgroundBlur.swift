import CoreImage
import CoreImage.CIFilterBuiltins
import QuartzCore
import Vision

/// Blurs everything but the person: Vision person segmentation (Neural Engine) for the mask,
/// Core Image for the blur and blend, rendered into an NV12 buffer for the encoder.
/// Used only on the capture queue.
final class BackgroundBlur {
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

    /// The mask is recomputed at most this often; at 60 fps every other frame reuses it.
    private static let maskInterval: CFTimeInterval = 1.0 / 30
    /// Gaussian sigma at strength 100 for a 1080-line frame.
    private static let maxSigma = 40.0

    /// Drops the cached mask (lens or blur toggled), so a stale one never shows up.
    func reset() {
        mask = nil
    }

    /// A new buffer with the background blurred, or nil to send the original.
    func process(_ pixelBuffer: CVPixelBuffer, strength: UInt8) -> CVPixelBuffer? {
        let width = CVPixelBufferGetWidth(pixelBuffer)
        let height = CVPixelBufferGetHeight(pixelBuffer)
        let now = CACurrentMediaTime()
        if mask == nil || now - maskTime >= Self.maskInterval {
            let handler = VNImageRequestHandler(cvPixelBuffer: pixelBuffer, orientation: .up)
            if (try? handler.perform([request])) != nil, let result = request.results?.first {
                mask = CIImage(cvPixelBuffer: result.pixelBuffer)
                maskTime = now
            }
        }
        guard let mask, let output = makeBuffer(width: width, height: height) else { return nil }

        let image = CIImage(cvPixelBuffer: pixelBuffer)
        let scaledMask = mask.transformed(by: CGAffineTransform(scaleX: CGFloat(width) / mask.extent.width,
                                                                y: CGFloat(height) / mask.extent.height))
        let sigma = Double(strength) / 100 * Self.maxSigma * Double(height) / 1080
        let background = image.clampedToExtent().applyingGaussianBlur(sigma: sigma).cropped(to: image.extent)
        let blend = CIFilter.blendWithMask()
        blend.inputImage = image
        blend.backgroundImage = background
        blend.maskImage = scaledMask
        guard let result = blend.outputImage else { return nil }
        context.render(result, to: output)
        return output
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
