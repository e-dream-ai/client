import Foundation

func extLog(_ message: String) {
    let stamp = ISO8601DateFormatter.string(
        from: Date(), timeZone: .current,
        formatOptions: [.withFullTime, .withFractionalSeconds])
    let line = "\(stamp) [infinidream-wallpaper] \(message)\n"
    fputs(line, stderr)
    NSLog("%@", line.trimmingCharacters(in: .newlines))

    // The app group container is not provisioned for every signing setup, so
    // log next to the engine logs (the appex has a sandbox exception there).
    let bid = Bundle.main.bundleIdentifier ?? "ai.infinidream.alpha.app.wallpaper"
    let root = bid.contains("stage") ? "/Users/Shared/infinidream.ai-stage" : "/Users/Shared/infinidream.ai"
    let dir = URL(fileURLWithPath: root).appendingPathComponent("Logs", isDirectory: true)
    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    let formatter = DateFormatter()
    formatter.locale = Locale(identifier: "en_US_POSIX")
    formatter.dateFormat = "yyyy_MM_dd"
    let path = dir.appendingPathComponent("wallpaper_ext_\(formatter.string(from: Date())).log")
    if let data = line.data(using: .utf8) {
        if FileManager.default.fileExists(atPath: path.path),
           let handle = try? FileHandle(forWritingTo: path) {
            defer { try? handle.close() }
            _ = try? handle.seekToEnd()
            try? handle.write(contentsOf: data)
        } else {
            try? data.write(to: path, options: .atomic)
        }
    }
}
