import Foundation

func extLog(_ message: String) {
    let line = "[infinidream-wallpaper] \(message)\n"
    fputs(line, stderr)
    NSLog("%@", line.trimmingCharacters(in: .newlines))

    // Sandboxed appex: write under the app group container.
    let bid = Bundle.main.bundleIdentifier ?? "ai.infinidream.alpha.app.wallpaper"
    let groupID = "group.\(bid)"
    guard let base = FileManager.default.containerURL(
        forSecurityApplicationGroupIdentifier: groupID
    ) else {
        return
    }
    let dir = base.appendingPathComponent("Logs", isDirectory: true)
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
