import SwiftUI
import UIKit

struct StatusView: View {
    let handler: ControlHandler
    @State private var dimTask: Task<Void, Never>?

    private var model: AppModel { handler.model }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
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
            }
            .padding(32)
        }
        .contentShape(Rectangle())
        .onTapGesture { wake() }
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true
            wake()
        }
    }

    /// Full brightness now, dim to 10% after 10 s without touches.
    private func wake() {
        UIScreen.main.brightness = 0.6
        dimTask?.cancel()
        dimTask = Task { @MainActor in
            try? await Task.sleep(for: .seconds(10))
            if !Task.isCancelled { UIScreen.main.brightness = 0.1 }
        }
    }
}
