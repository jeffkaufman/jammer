// speechcompile -- build the speech recognizer's dictionary, from inside the
// App Sandbox.
//
//   speechcompile --home   prints this helper's container, where its files go
//   speechcompile DIR      compiles DIR/model.bin into DIR/lm and DIR/vocab
//
// The dictionary (speech-model.bin, from speechmodel.swift) is training
// data; macOS compiles it into a language model on the Mac that uses it.  As
// of macOS 27.2 the speech service will only do that for a process in the
// App Sandbox -- "Caller lacks write access to outDirectory", in its log,
// whatever the directory -- and jammer isn't, so jammer runs this instead
// (speech.h's speech_prepare_dictionary).  Sandboxed, it can only read and
// write its own container, so DIR has to be in there: jammer asks for
// --home, puts the training data under it, and reads the model back from
// beside it.
//
// Built by the Makefile with its Info.plist linked in, for the bundle
// identifier the container is named after, and signed with
// speechcompile.entitlements, which is what puts it in the sandbox.

import Foundation
import Speech

let args = CommandLine.arguments
guard args.count == 2 else {
  FileHandle.standardError.write("usage: speechcompile --home | DIR\n".data(using: .utf8)!)
  exit(2)
}
if args[1] == "--home" {
  print(NSHomeDirectory())
  exit(0)
}

let dir = URL(fileURLWithPath: args[1])
let config = SFSpeechLanguageModel.Configuration(
  languageModel: dir.appendingPathComponent("lm"),
  vocabulary: dir.appendingPathComponent("vocab"))
let done = DispatchSemaphore(value: 0)
var failure: String? = nil
SFSpeechLanguageModel.prepareCustomLanguageModel(
  for: dir.appendingPathComponent("model.bin"), configuration: config
) { error in
  if let error = error { failure = error.localizedDescription }
  done.signal()
}
if done.wait(timeout: .now() + 600) == .timedOut { failure = "timed out" }
if let failure = failure {
  FileHandle.standardError.write("\(failure)\n".data(using: .utf8)!)
  exit(1)
}
print("ok")
