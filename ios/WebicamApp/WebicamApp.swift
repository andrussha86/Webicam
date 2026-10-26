import AVFoundation
import SwiftUI

@main
struct WebicamApp: App {
    private let handler = ControlHandler()
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            StatusView(handler: handler)
                .task {
                    _ = await AVCaptureDevice.requestAccess(for: .video)
                    handler.start()
                }
                .onChange(of: scenePhase) { _, phase in
                    if phase == .active { handler.start() }
                }
        }
    }
}
