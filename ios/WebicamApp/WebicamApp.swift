import AVFoundation
import SwiftUI

@main
struct WebicamApp: App {
    private let handler = ControlHandler()

    var body: some Scene {
        WindowGroup {
            StatusView(handler: handler)
                .task {
                    _ = await AVCaptureDevice.requestAccess(for: .video)
                    handler.start()
                }
        }
    }
}
