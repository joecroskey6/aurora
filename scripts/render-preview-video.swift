#!/usr/bin/env swift
// Usage: aurora-render-clip output.mp4 [PNG-directory] [capture-report.json]
// Default input: /tmp/aurora-flow-clip/frame-%04d.png, at exactly 30 fps.
// Build: swiftc -O -sdk /Library/Developer/CommandLineTools/SDKs/MacOSX13.3.sdk \
//   -module-cache-path /tmp/aurora-swift-module-cache \
//   scripts/render-preview-video.swift -o /tmp/aurora-render-clip

import Foundation
import AVFoundation
import CoreGraphics
import CoreVideo
import ImageIO
import Accelerate

struct ClipError: Error, CustomStringConvertible {
    let description: String
    init(_ message: String) { description = message }
}

func loadPNG(_ url: URL) throws -> CGImage {
    guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
        throw ClipError("Cannot decode PNG: \(url.path)")
    }
    return image
}

func makePixelBuffer(image: CGImage, pool: CVPixelBufferPool,
                     width: Int, height: Int) throws -> CVPixelBuffer {
    guard image.width == width, image.height == height else {
        throw ClipError("All frames must be \(width)×\(height); found \(image.width)×\(image.height).")
    }
    var optionalBuffer: CVPixelBuffer?
    let allocation = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &optionalBuffer)
    guard allocation == kCVReturnSuccess, let pixelBuffer = optionalBuffer else {
        throw ClipError("Pixel buffer allocation failed: \(allocation)")
    }

    // vImage converts the CGImage's existing scanlines directly, avoiding Quartz
    // drawing coordinates and their common accidental vertical flip. BGRA matches
    // kCVPixelFormatType_32BGRA; no mirroring, rotation, scaling or row reversal.
    // Premultiplication composites any transparency over black in the H.264 clip.
    let colourSpace = CGColorSpace(name: CGColorSpace.sRGB)!
    CVBufferSetAttachment(pixelBuffer, kCVImageBufferCGColorSpaceKey,
                          colourSpace, .shouldPropagate)
    CVBufferSetAttachment(pixelBuffer, kCVImageBufferColorPrimariesKey,
                          kCVImageBufferColorPrimaries_ITU_R_709_2, .shouldPropagate)
    CVBufferSetAttachment(pixelBuffer, kCVImageBufferTransferFunctionKey,
                          kCVImageBufferTransferFunction_sRGB, .shouldPropagate)
    var format = vImage_CGImageFormat(
        bitsPerComponent: 8,
        bitsPerPixel: 32,
        colorSpace: Unmanaged.passUnretained(colourSpace),
        bitmapInfo: CGBitmapInfo(rawValue: CGBitmapInfo.byteOrder32Little.rawValue
            | CGImageAlphaInfo.premultipliedFirst.rawValue),
        version: 0, decode: nil, renderingIntent: .defaultIntent)
    var converted = vImage_Buffer()
    let conversion = vImageBuffer_InitWithCGImage(&converted, &format, nil, image,
                                                 vImage_Flags(kvImageNoFlags))
    guard conversion == kvImageNoError, converted.data != nil else {
        if converted.data != nil { free(converted.data) }
        throw ClipError("Image conversion failed: \(conversion)")
    }
    defer { free(converted.data) }

    let lock = CVPixelBufferLockBaseAddress(pixelBuffer, [])
    guard lock == kCVReturnSuccess, let destination = CVPixelBufferGetBaseAddress(pixelBuffer) else {
        if lock == kCVReturnSuccess { CVPixelBufferUnlockBaseAddress(pixelBuffer, []) }
        throw ClipError("Pixel buffer mapping failed: \(lock)")
    }
    defer { CVPixelBufferUnlockBaseAddress(pixelBuffer, []) }
    let destinationStride = CVPixelBufferGetBytesPerRow(pixelBuffer)
    for row in 0..<height {
        memcpy(destination.advanced(by: row * destinationStride),
               converted.data.advanced(by: row * converted.rowBytes), width * 4)
    }
    return pixelBuffer
}

func encode() throws {
    let arguments = CommandLine.arguments
    guard (2...4).contains(arguments.count) else {
        throw ClipError("Usage: \(arguments[0]) output.mp4 [PNG-directory] [capture-report.json]")
    }
    let fileManager = FileManager.default
    let output = URL(fileURLWithPath: arguments[1]).standardizedFileURL
    let input = URL(fileURLWithPath: arguments.count >= 3 ? arguments[2] : "/tmp/aurora-flow-clip",
                    isDirectory: true)
    guard output.pathExtension.lowercased() == "mp4" else {
        throw ClipError("Destination must have an .mp4 extension.")
    }
    guard !fileManager.fileExists(atPath: output.path) else {
        throw ClipError("Destination already exists: \(output.path). Choose a new path or remove it explicitly.")
    }
    let matcher = try NSRegularExpression(pattern: "^frame-([0-9]{4,})\\.png$")
    let urls = try fileManager.contentsOfDirectory(at: input, includingPropertiesForKeys: nil)
    var frames: [(number: Int, url: URL)] = []
    for url in urls {
        let name = url.lastPathComponent as NSString
        if let match = matcher.firstMatch(in: name as String, range: NSRange(location: 0, length: name.length)),
           let number = Int(name.substring(with: match.range(at: 1))) {
            frames.append((number, url))
        }
    }
    frames.sort { $0.number < $1.number }
    guard let first = frames.first else {
        throw ClipError("No frame-%04d.png images found in \(input.path).")
    }
    for index in 1..<frames.count where frames[index].number != frames[index - 1].number + 1 {
        throw ClipError("Frame sequence has a duplicate or gap near \(frames[index].url.lastPathComponent).")
    }
    // Optional native capture timestamps preserve actual frame pacing. The
    // first captured frame starts at zero; late intervals are not sped up.
    var presentationTimes = frames.indices.map { Double($0) / 30.0 }
    var endSeconds = Double(frames.count) / 30.0
    if arguments.count == 4 {
        let data = try Data(contentsOf: URL(fileURLWithPath: arguments[3]))
        guard let report = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let records = report["frames"] as? [[String: Any]], records.count == frames.count,
              let duration = report["requestedDurationSeconds"] as? Double else {
            throw ClipError("Capture report must match the numbered frames and include duration.")
        }
        var timestamps: [Double] = []
        for (index, record) in records.enumerated() {
            guard let stamp = record["actualTimestampSeconds"] as? Double,
                  stamp.isFinite, stamp >= 0,
                  let number = record["frame"] as? Int, number == frames[index].number,
                  index == 0 || stamp > timestamps[index - 1] else {
                throw ClipError("Invalid or non-monotonic capture timestamp at frame \(index).")
            }
            timestamps.append(stamp)
        }
        let origin = timestamps[0]
        presentationTimes = timestamps.map { $0 - origin }
        endSeconds = max(duration - origin, presentationTimes.last! + 0.001)
    }
    let firstImage = try loadPNG(first.url)
    let width = firstImage.width, height = firstImage.height
    guard width > 0, height > 0, width % 2 == 0, height % 2 == 0 else {
        throw ClipError("H.264 requires positive even dimensions; first frame is \(width)×\(height).")
    }
    try fileManager.createDirectory(at: output.deletingLastPathComponent(), withIntermediateDirectories: true)

    let writer = try AVAssetWriter(outputURL: output, fileType: .mp4)
    writer.shouldOptimizeForNetworkUse = true
    let fps: Int32 = 30
    let bitRate = max(8_000_000, min(40_000_000, width * height * 18))
    let compression: [String: Any] = [
        AVVideoAverageBitRateKey: bitRate,
        AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
        AVVideoExpectedSourceFrameRateKey: fps,
        AVVideoMaxKeyFrameIntervalKey: Int(fps) * 2,
        AVVideoAllowFrameReorderingKey: false
    ]
    let settings: [String: Any] = [
        AVVideoCodecKey: AVVideoCodecType.h264,
        AVVideoWidthKey: width,
        AVVideoHeightKey: height,
        AVVideoCompressionPropertiesKey: compression,
        AVVideoColorPropertiesKey: [
            AVVideoColorPrimariesKey: AVVideoColorPrimaries_ITU_R_709_2,
            AVVideoTransferFunctionKey: AVVideoTransferFunction_ITU_R_709_2,
            AVVideoYCbCrMatrixKey: AVVideoYCbCrMatrix_ITU_R_709_2
        ]
    ]
    let videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
    videoInput.expectsMediaDataInRealTime = false
    let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: videoInput,
        sourcePixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height,
            kCVPixelBufferIOSurfacePropertiesKey as String: [String: Any]()
        ])
    guard writer.canAdd(videoInput) else { throw ClipError("H.264 video input is unavailable.") }
    writer.add(videoInput)
    guard writer.startWriting() else {
        throw ClipError("Cannot start writer: \(writer.error?.localizedDescription ?? "unknown error")")
    }
    var completed = false
    defer { if !completed { writer.cancelWriting() } }
    writer.startSession(atSourceTime: .zero)
    guard let pool = adaptor.pixelBufferPool else { throw ClipError("Writer did not create a pixel buffer pool.") }

    for (index, frame) in frames.enumerated() {
        try autoreleasepool {
            let image = index == 0 ? firstImage : try loadPNG(frame.url)
            let pixelBuffer = try makePixelBuffer(image: image, pool: pool, width: width, height: height)
            let deadline = Date().addingTimeInterval(15)
            while !videoInput.isReadyForMoreMediaData {
                guard writer.status == .writing, Date() < deadline else {
                    throw ClipError("Encoder stopped accepting frames: \(writer.error?.localizedDescription ?? "timeout")")
                }
                Thread.sleep(forTimeInterval: 0.002)
            }
            let time = CMTime(seconds: presentationTimes[index], preferredTimescale: 600_000)
            guard adaptor.append(pixelBuffer, withPresentationTime: time) else {
                throw ClipError("Failed at \(frame.url.lastPathComponent): \(writer.error?.localizedDescription ?? "append failed")")
            }
        }
    }
    writer.endSession(atSourceTime: CMTime(seconds: endSeconds, preferredTimescale: 600_000))
    videoInput.markAsFinished()
    let finished = DispatchSemaphore(value: 0)
    writer.finishWriting { finished.signal() }
    guard finished.wait(timeout: .now() + 30) == .success, writer.status == .completed else {
        throw ClipError("Cannot finish clip: \(writer.error?.localizedDescription ?? "timeout")")
    }
    completed = true
    let duration = String(format: "%.3f", endSeconds)
    print("Wrote \(frames.count) frames, \(width)×\(height), \(arguments.count == 4 ? "capture timing" : "30 fps"), \(duration)s, H.264 without audio: \(output.path)")
}

do {
    try encode()
} catch {
    FileHandle.standardError.write(Data("Preview encoder: \(error)\n".utf8))
    exit(1)
}
