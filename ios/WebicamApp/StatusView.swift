import SwiftUI
import UIKit

struct StatusView: View {
    let handler: ControlHandler
    @Environment(\.scenePhase) private var scenePhase
    @State private var dimTask: Task<Void, Never>?
    /// Screen "off": nothing drawn on the black OLED and brightness at minimum.
    /// iOS stops the camera when the screen locks, so this is as dark as it gets.
    @State private var dimmed = false
    @State private var userBrightness: CGFloat?

    private var model: AppModel { handler.model }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            if !dimmed {
                VStack(spacing: 16) {
                    Text(model.connected ? "Подключено к ПК" : "Ожидание ПК…")
                        .font(.title2.weight(.semibold))
                        .foregroundStyle(model.connected ? .green : .white)
                    if model.connected {
                        Text(model.format).foregroundStyle(.white)
                        Text(model.bitrate).foregroundStyle(.gray)
                    } else {
                        Text("Подключите iPhone кабелем и запустите Webicam на компьютере")
                            .multilineTextAlignment(.center)
                            .foregroundStyle(.gray)
                    }
                    if let warning = model.warning {
                        Text(warning).foregroundStyle(.orange)
                    }
                    Button(model.stopped ? "Старт" : "Стоп") { handler.toggleStopped() }
                        .buttonStyle(.borderedProminent)
                        .tint(model.stopped ? .green : .red)
                        .padding(.top, 24)
                    Text("Экран погаснет через 10 с, коснитесь, чтобы включить")
                        .font(.footnote)
                        .foregroundStyle(.gray)
                        .padding(.top, 24)
                }
                .padding(32)
            }
        }
        .contentShape(Rectangle())
        .onTapGesture { wake() }
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true
            wake()
        }
        .onChange(of: scenePhase) { _, phase in
            if phase == .active {
                wake()
            } else {
                undim()  // iOS keeps our brightness after we leave; give the user theirs back
            }
        }
    }

    /// Shows the screen now and turns it "off" after 10 s without touches.
    private func wake() {
        undim()
        dimTask = Task { @MainActor in
            try? await Task.sleep(for: .seconds(10))
            guard !Task.isCancelled else { return }
            userBrightness = UIScreen.main.brightness
            UIScreen.main.brightness = 0
            dimmed = true
        }
    }

    private func undim() {
        dimTask?.cancel()
        dimTask = nil
        if dimmed, let userBrightness { UIScreen.main.brightness = userBrightness }
        dimmed = false
    }
}
