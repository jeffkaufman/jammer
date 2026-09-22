// speechmodel.swift -- build the speech recognizer's custom language model,
// its dictionary, from the phrases speechphrases.c prints:
//
//   ./speechphrases | ./speechmodel speech-model.bin
//
// Swift because Apple only offers SFCustomLanguageModelData from Swift.  What
// comes out is training data, not the model itself: speech.h compiles it on
// the Mac that runs it, the first time and whenever it changes.
//
// The counts say how much more likely a phrase is than ordinary English.
// They're large because the point is for "press foot bass" to beat "press
// foot base", and "arpeggiator" to beat "or educator".

import Foundation
import Speech

guard CommandLine.arguments.count == 2 else {
  FileHandle.standardError.write("usage: speechphrases | speechmodel OUT\n"
                                   .data(using: .utf8)!)
  exit(1)
}
let out = URL(fileURLWithPath: CommandLine.arguments[1])

var classes: [String: [String]] = [:]
var numbers: [String] = []
var pronunciations: [(String, String)] = []
var input = ""
while let line = readLine() {
  input += line + "\n"
  let fields = line.components(separatedBy: "\t")
  switch fields[0] {
  case "button", "key", "mode": classes[fields[0], default: []].append(fields[1])
  case "number": numbers.append(fields[1])
  case "pron": pronunciations.append((fields[1], fields[2]))
  default: break
  }
}

// The version is the input, hashed, so a changed dictionary is a new model
// rather than one the system thinks it has already compiled.
var hash: UInt64 = 1469598103934665603
for byte in input.utf8 { hash = (hash ^ UInt64(byte)) &* 1099511628211 }

let data = SFCustomLanguageModelData(
  locale: Locale(identifier: "en-US"),
  identifier: "com.jefftk.jammer.speech",
  version: String(hash, radix: 16))

let templates = SFCustomLanguageModelData.TemplatePhraseCountGenerator()
for (name, values) in classes { templates.define(className: name, values: values) }
templates.insert(template: "press <button>", count: 50000)
templates.insert(template: "select <button>", count: 10000)
templates.insert(template: "change key to <key>", count: 10000)
templates.insert(template: "change mode to <mode>", count: 10000)
data.insert(phraseCountGenerator: templates)

for number in numbers {
  data.insert(phraseCount: .init(phrase: number, count: 20000))
}
for (word, phonemes) in pronunciations {
  data.insert(term: .init(grapheme: word, phonemes: [phonemes]))
}

try await data.export(to: out)
print("wrote \(out.path): \(classes.mapValues(\.count)) and "
      + "\(numbers.count) numbers, \(pronunciations.count) pronunciations")
