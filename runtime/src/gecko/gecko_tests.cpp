// Checks for the Gecko text parser, codetype table, memory backend and
// interpreter. Built as a separate host executable (mh3_gecko_check); it does
// not link the runtime or touch Ocean's guest memory.

#include "gecko/gecko_compat.h"
#include "gecko/gecko_interpreter.h"
#include "gecko/gecko_memory.h"
#include "gecko/gecko_parser.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

bool HasError(const std::vector<Gecko::Diagnostic>& diagnostics) {
    for (const Gecko::Diagnostic& diagnostic : diagnostics)
        if (diagnostic.severity == Gecko::DiagnosticSeverity::Error)
            return true;

    return false;
}

bool HasFileError(const Gecko::ParseResult& result) {
    return HasError(result.diagnostics);
}

bool HasCodeError(const Gecko::Code& code) {
    return HasError(code.diagnostics);
}

bool HasDiagnostic(const std::vector<Gecko::Diagnostic>& diagnostics, std::string_view needle) {
    for (const Gecko::Diagnostic& diagnostic : diagnostics)
        if (diagnostic.message.find(needle) != std::string::npos)
            return true;

    return false;
}

std::string HexWord(uint32_t value) {
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

void TestOneValidCode() {
    const auto result = Gecko::Parse(
        "$Infinite Health\n"
        "* Keeps health full.\n"
        "04000000 00000064\n");

    Check(result.codes.size() == 1, "one code");
    Check(!HasFileError(result), "one code: no file error");

    if (result.codes.size() == 1) {
        const Gecko::Code& code = result.codes[0];
        Check(code.name == "Infinite Health", "one code: name");
        Check(code.instructions.size() == 1, "one code: one instruction");
        Check(code.instructions[0].codetype == uint8_t(Gecko::Codetype::Write32), "one code: write32 type");
        Check(code.instructions[0].address == 0x00000000u, "one code: address");
        Check(code.support == Gecko::SupportLevel::Supported, "one code: supported");
        Check(code.valid, "one code: valid");
        Check(!HasCodeError(code), "one code: no code error");
        Check(code.description.find("Keeps health full.") != std::string::npos, "one code: description");
        Check(code.sourceText.find("$Infinite Health") != std::string::npos, "one code: source text");
        Check(code.id != 0, "one code: id");
    }
}

void TestMultipleCodes() {
    const auto result = Gecko::Parse(
        "$First\n"
        "04000000 00000001\n"
        "$Second\n"
        "02000000 00000002\n");

    Check(result.codes.size() == 2, "multiple codes: count");
    Check(!HasFileError(result), "multiple codes: no file error");

    if (result.codes.size() == 2) {
        Check(result.codes[0].name == "First", "multiple codes: first name");
        Check(result.codes[1].name == "Second", "multiple codes: second name");
        Check(result.codes[0].id != result.codes[1].id, "multiple codes: distinct ids");
    }
}

void TestCommentsAndBlankLines() {
    const auto result = Gecko::Parse(
        "* a file comment\n"
        "\n"
        "$Code A\n"
        "* an inner comment\n"
        "\n"
        "04000000 00000001\n");

    Check(result.codes.size() == 1, "comments: count");
    Check(result.headerText.find("a file comment") != std::string::npos, "comments: header kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].comments.size() == 1, "comments: inner comment kept");
        Check(result.codes[0].description.find("an inner comment") != std::string::npos, "comments: description");
    }
}

void TestCrlfAndLf() {
    const auto crlf = Gecko::Parse("$Code\r\n04000000 00000001\r\n");
    const auto lf = Gecko::Parse("$Code\n04000000 00000001\n");

    Check(crlf.codes.size() == 1, "crlf: count");
    Check(lf.codes.size() == 1, "lf: count");
    Check(!HasFileError(crlf), "crlf: no file error");
    Check(!HasFileError(lf), "lf: no file error");
}

void TestInvalidHex() {
    const auto result = Gecko::Parse("$Bad\n04GGGGGG 00000000\n");

    Check(result.codes.size() == 1, "invalid hex: code kept");
    Check(HasFileError(result) == false, "invalid hex: code-level not file-level");

    if (result.codes.size() == 1) {
        Check(HasCodeError(result.codes[0]), "invalid hex: error diagnostic");
        Check(!result.codes[0].valid, "invalid hex: not valid");
        Check(HasDiagnostic(result.codes[0].diagnostics, "invalid hexadecimal"), "invalid hex: message");
    }
}

void TestMissingWords() {
    const auto result = Gecko::Parse("$Bad\n04000000\n");

    Check(result.codes.size() == 1, "missing words: code kept");

    if (result.codes.size() == 1) {
        Check(HasCodeError(result.codes[0]), "missing words: error diagnostic");
        Check(!result.codes[0].valid, "missing words: not valid");
        Check(HasDiagnostic(result.codes[0].diagnostics, "two hexadecimal"), "missing words: message");
    }
}

void TestIncompleteSerial() {
    const auto result = Gecko::Parse("$Bad\n08000000 00000000\n");

    Check(result.codes.size() == 1, "incomplete serial: code kept");

    if (result.codes.size() == 1) {
        Check(HasCodeError(result.codes[0]), "incomplete serial: error diagnostic");
        Check(HasDiagnostic(result.codes[0].diagnostics, "incomplete"), "incomplete serial: message");
    }
}

void TestUnknownCodetype() {
    const auto result = Gecko::Parse("$Unknown\n0A000000 00000000\n");

    Check(result.codes.size() == 1, "unknown codetype: code kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].support == Gecko::SupportLevel::Unknown, "unknown codetype: support unknown");
        Check(HasDiagnostic(result.codes[0].diagnostics, "unknown Gecko codetype"), "unknown codetype: message");
        Check(result.codes[0].valid, "unknown codetype: syntactically valid");
    }
}

void TestUnsupportedCodetype() {
    const auto result = Gecko::Parse(
        "$Insert ASM\n"
        "C2000000 00000001\n"
        "60000000 00000000\n"
        "00000000 00000000\n");

    Check(result.codes.size() == 1, "unsupported: code kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].support == Gecko::SupportLevel::Unsupported, "unsupported: support");
        Check(HasDiagnostic(result.codes[0].diagnostics, "unsupported Gecko codetype"), "unsupported: message");
        Check(result.codes[0].instructions.size() == 1, "unsupported: one instruction block");
    }
}

void TestExecuteAsm() {
    const auto result = Gecko::Parse(
        "$Execute ASM\n"
        "C0000000 00000001\n"
        "60000000 00000000\n"
        "4E800020 00000000\n");

    Check(result.codes.size() == 1, "execute asm: code kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].support == Gecko::SupportLevel::Unsupported, "execute asm: support");
        Check(result.codes[0].instructions.size() == 1, "execute asm: one instruction block");
        Check(result.codes[0].instructions[0].extraWords.size() == 4, "execute asm: block kept");
        Check(!HasDiagnostic(result.codes[0].diagnostics, "does not end with blr"), "execute asm: terminator accepted");
    }
}

void TestSupportedCodetype() {
    const auto result = Gecko::Parse("$Write\n04000000 00000001\n");

    Check(result.codes.size() == 1, "supported: code kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].support == Gecko::SupportLevel::Supported, "supported: support");
        Check(!HasCodeError(result.codes[0]), "supported: no code error");
    }
}

void TestRegionMismatch() {
    const auto mismatch = Gecko::Parse("RMHE08\n$Code\n04000000 00000000\n");
    const auto match = Gecko::Parse("RMHP08\n$Code\n04000000 00000000\n");
    const auto unknown = Gecko::Parse("$Code\n04000000 00000000\n");

    Check(mismatch.gameId == "RMHE08", "region: game id kept");
    Check(mismatch.region == "US", "region: derived");
    Check(mismatch.codes.size() == 1, "region: code kept");
    Check(mismatch.codes.size() == 1 && mismatch.codes[0].gameCompatibility == Gecko::GameCompatibility::RegionMismatch, "region: mismatch");

    Check(match.codes.size() == 1 && match.codes[0].gameCompatibility == Gecko::GameCompatibility::Match, "region: match");

    Check(unknown.codes.size() == 1 && unknown.codes[0].gameCompatibility == Gecko::GameCompatibility::Unknown, "region: unknown");
}

void TestEmptyInput() {
    const auto result = Gecko::Parse("");

    Check(result.codes.empty(), "empty: no codes");
    Check(HasDiagnostic(result.diagnostics, "no Gecko codes"), "empty: warning");
}

void TestLargeInput() {
    std::string text;

    for (int i = 0; i < 1000; ++i) {
        text += "$Code " + std::to_string(i) + "\n";
        text += "04000000 " + HexWord(uint32_t(i)) + "\n";
    }

    const auto result = Gecko::Parse(text);
    Check(result.codes.size() == 1000, "large: 1000 codes");
    Check(!HasFileError(result), "large: no file error");

    std::string huge(5u * 1024u * 1024u, 'x');
    const auto rejected = Gecko::Parse(huge);
    Check(rejected.codes.empty(), "large: oversized rejected");
    Check(HasFileError(rejected), "large: oversized error");
}

void TestLimits() {
    Gecko::ParseOptions instructionLimit;
    instructionLimit.maxInstructionsPerCode = 1;
    const auto tooMany = Gecko::Parse(
        "$Two\n"
        "04000000 00000001\n"
        "04000000 00000002\n",
        instructionLimit);
    Check(tooMany.codes.size() == 1, "limits: code kept");
    Check(tooMany.codes.size() == 1 && HasDiagnostic(tooMany.codes[0].diagnostics, "too many instructions"), "limits: instruction cap");

    Gecko::ParseOptions codeLimit;
    codeLimit.maxCodes = 1;
    const auto many = Gecko::Parse(
        "$A\n"
        "04000000 00000001\n"
        "$B\n"
        "04000000 00000002\n",
        codeLimit);
    Check(many.codes.size() == 1, "limits: code cap");
    Check(HasFileError(many), "limits: code cap error");
}

void TestMalformedInput() {
    const auto result = Gecko::Parse(
        "not a code\n"
        "$Broken\n"
        "zzzzzzzz zzzzzzzz\n"
        "0400 0000\n");

    Check(result.codes.size() == 1, "malformed: code kept");
    Check(!result.diagnostics.empty(), "malformed: file diagnostics");

    if (result.codes.size() == 1)
        Check(HasCodeError(result.codes[0]), "malformed: code error");
}

void TestGameIdMetadata() {
    const auto result = Gecko::Parse(
        "Game ID: RMHE08\n"
        "$Code\n"
        "04000000 00000000\n");

    Check(result.gameId == "RMHE08", "metadata: game id");
    Check(result.region == "US", "metadata: region");
    Check(result.codes.size() == 1 && result.codes[0].gameId == "RMHE08", "metadata: code game id");
    Check(result.codes.size() == 1 && result.codes[0].gameCompatibility == Gecko::GameCompatibility::RegionMismatch, "metadata: compatibility");
}

void TestDuplicateIds() {
    const auto result = Gecko::Parse(
        "$Same\n"
        "04000000 00000001\n"
        "$Same\n"
        "04000000 00000001\n");

    Check(result.codes.size() == 2, "duplicate: two codes");
    Check(result.codes.size() == 2 && result.codes[0].id == result.codes[1].id, "duplicate: same id");

    if (result.codes.size() == 2)
        Check(HasDiagnostic(result.codes[1].diagnostics, "duplicate code id"), "duplicate: warning");
}

void TestStringWrite() {
    const auto result = Gecko::Parse(
        "$String\n"
        "06000000 00000005\n"
        "41424344 45000000\n");

    Check(result.codes.size() == 1, "string: code kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].support == Gecko::SupportLevel::Supported, "string: support");
        Check(result.codes[0].valid, "string: valid");
        Check(result.codes[0].instructions.size() == 1, "string: one instruction");
        Check(result.codes[0].instructions[0].extraWords.size() == 2, "string: data words kept");
    }
}

void TestSerial() {
    const auto result = Gecko::Parse(
        "$Serial\n"
        "08000000 00000000\n"
        "00000000 00000000\n");

    Check(result.codes.size() == 1, "serial: code kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].support == Gecko::SupportLevel::Supported, "serial: support");
        Check(result.codes[0].instructions.size() == 1, "serial: one instruction");
        Check(result.codes[0].instructions[0].extraWords.size() == 2, "serial: operands kept");
        Check(result.codes[0].valid, "serial: valid");
    }
}

void TestCounterIf() {
    const auto result = Gecko::Parse(
        "$Counter\n"
        "A8000000 00000000\n");

    Check(result.codes.size() == 1, "counter: code kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].support == Gecko::SupportLevel::Supported, "counter: support");
        Check(result.codes[0].instructions.size() == 1, "counter: one instruction");
        Check(result.codes[0].instructions[0].extraWords.empty(), "counter: single-line instruction");
    }
}

void TestSearch() {
    const auto result = Gecko::Parse(
        "$Search\n"
        "F6000001 80000000\n"
        "00000000 00000000\n");

    Check(result.codes.size() == 1, "search: code kept");

    if (result.codes.size() == 1) {
        Check(result.codes[0].support == Gecko::SupportLevel::Supported, "search: status");
        Check(!HasDiagnostic(result.codes[0].diagnostics, "unsupported"), "search: no unsupported message");
    }
}

void TestInstructionOutsideCode() {
    const auto result = Gecko::Parse("04000000 00000000\n$Code\n04000000 00000000\n");

    Check(HasFileError(result), "outside: file error");
    Check(HasDiagnostic(result.diagnostics, "outside a code"), "outside: message");
}

void TestEmptyCodeName() {
    const auto result = Gecko::Parse("$\n04000000 00000000\n");

    Check(result.codes.size() == 1, "empty name: code kept");

    if (result.codes.size() == 1)
        Check(HasDiagnostic(result.codes[0].diagnostics, "empty name"), "empty name: warning");
}

void TestNoInstructions() {
    const auto result = Gecko::Parse("$Empty\n* only a comment\n");

    Check(result.codes.size() == 1, "no instructions: code kept");

    if (result.codes.size() == 1) {
        Check(!result.codes[0].valid, "no instructions: not valid");
        Check(HasDiagnostic(result.codes[0].diagnostics, "no instructions"), "no instructions: message");
    }
}

void TestCompatibilityTable() {
    const Gecko::CodetypeInfo write32 = Gecko::ClassifyCodetype(uint8_t(Gecko::Codetype::Write32));
    const Gecko::CodetypeInfo insert = Gecko::ClassifyCodetype(uint8_t(Gecko::Codetype::InsertAsm));
    const Gecko::CodetypeInfo search = Gecko::ClassifyCodetype(uint8_t(Gecko::Codetype::SearchSetPointer));
    const Gecko::CodetypeInfo unknown = Gecko::ClassifyCodetype(0x05);

    Check(write32.support == Gecko::SupportLevel::Supported, "table: write32 supported");
    Check(insert.support == Gecko::SupportLevel::Unsupported, "table: insert asm unsupported");
    Check(search.support == Gecko::SupportLevel::Supported, "table: search supported");
    Check(unknown.support == Gecko::SupportLevel::Unknown, "table: unknown");

    Check(Gecko::RegionFromGameId("RMHE08") == "US", "table: US region");
    Check(Gecko::RegionFromGameId("RMHP08") == "EU", "table: EU region");
    Check(Gecko::RegionFromGameId("RMHJ08") == "JP", "table: JP region");
    Check(Gecko::RegionFromGameId("bad").empty(), "table: short id");

    Check(Gecko::ClassifyGameCompatibility("RMHP08", "RMHP08") == Gecko::GameCompatibility::Match, "table: same game");
    Check(Gecko::ClassifyGameCompatibility("RMHE08", "RMHP08") == Gecko::GameCompatibility::RegionMismatch, "table: region mismatch");
    Check(Gecko::ClassifyGameCompatibility("RMCE01", "RMHP08") == Gecko::GameCompatibility::Unknown, "table: different game");
    Check(Gecko::ClassifyGameCompatibility("", "RMHP08") == Gecko::GameCompatibility::Unknown, "table: no id");
}


Gecko::Code ParseOne(const char* text) {
    const Gecko::ParseResult result = Gecko::Parse(text);

    if (result.codes.empty()) {
        Check(false, "interpreter test: code parsed");
        return {};
    }

    return result.codes[0];
}

Gecko::ExecutionResult RunCode(const Gecko::Code& code, Gecko::Memory& memory) {
    Gecko::State state;
    state.Reset(code);
    return Gecko::Interpreter::Run(code, memory, state);
}

void TestInterpreterWrites() {
    Gecko::FlatMemory memory(0x80000000u, 0x2000u);

    const auto write8 = ParseOne("$W8\n00000010 0000002A\n");
    const auto r8 = RunCode(write8, memory);
    uint8_t b = 0;
    Check(r8.ok(), "interp: 8-bit write succeeds");
    Check(memory.Read8(0x80000010, b) && b == 0x2A, "interp: 8-bit write value");

    const auto write16 = ParseOne("$W16\n02000020 00001234\n");
    const auto r16 = RunCode(write16, memory);
    uint16_t h = 0;
    Check(r16.ok(), "interp: 16-bit write succeeds");
    Check(memory.Read16(0x80000020, h) && h == 0x1234, "interp: 16-bit write value");

    const auto write32 = ParseOne("$W32\n04000030 DEADBEEF\n");
    const auto r32 = RunCode(write32, memory);
    uint32_t w = 0;
    Check(r32.ok(), "interp: 32-bit write succeeds");
    Check(memory.Read32(0x80000030, w) && w == 0xDEADBEEFu, "interp: 32-bit write value");

    const auto fill = ParseOne("$Fill\n00000040 000300AA\n");
    const auto rf = RunCode(fill, memory);
    Check(rf.ok(), "interp: fill succeeds");
    for (uint32_t i = 0; i < 4; ++i)
        Check(memory.Read8(0x80000040 + i, b) && b == 0xAA, "interp: fill byte");

    const auto string = ParseOne("$String\n06000050 00000005\n41424344 45000000\n");
    const auto rs = RunCode(string, memory);
    Check(rs.ok(), "interp: string write succeeds");
    Check(memory.Read8(0x80000050, b) && b == 'A', "interp: string first byte");
    Check(memory.Read8(0x80000054, b) && b == 'E', "interp: string last byte");

    const auto serial = ParseOne("$Serial\n08000060 00000001\n20010004 00000010\n");
    const auto rr = RunCode(serial, memory);
    Check(rr.ok(), "interp: serial write succeeds");
    Check(memory.Read32(0x80000060, w) && w == 1, "interp: serial first value");
    Check(memory.Read32(0x80000064, w) && w == 0x11, "interp: serial second value");
}

void TestInterpreterConditions() {
    Gecko::FlatMemory memory(0x80000000u, 0x2000u);
    uint32_t w = 0;

    const auto execute = ParseOne(
        "$If true\n"
        "04000070 00000001\n"
        "20000070 00000001\n"
        "04000074 00000002\n"
        "E0000000 00000000\n");
    Check(RunCode(execute, memory).ok(), "interp: true if succeeds");
    Check(memory.Read32(0x80000074, w) && w == 2, "interp: true if writes");

    const auto skip = ParseOne(
        "$If false\n"
        "04000080 00000001\n"
        "20000080 00000002\n"
        "04000084 00000003\n"
        "E0000000 00000000\n");
    Check(RunCode(skip, memory).ok(), "interp: false if succeeds");
    Check(memory.Read32(0x80000084, w) && w == 0, "interp: false if skips");

    const auto nested = ParseOne(
        "$Nested\n"
        "04000090 00000001\n"
        "20000090 00000001\n"
        "20000090 00000001\n"
        "04000094 00000004\n"
        "E0000000 00000000\n");
    Check(RunCode(nested, memory).ok(), "interp: nested if succeeds");
    Check(memory.Read32(0x80000094, w) && w == 4, "interp: nested if writes");

    const auto endif = ParseOne(
        "$Endif\n"
        "040000A0 00000001\n"
        "200000A0 00000001\n"
        "040000A4 00000005\n"
        "E2000001 00000000\n"
        "040000A8 00000006\n");
    Check(RunCode(endif, memory).ok(), "interp: endif succeeds");
    Check(memory.Read32(0x800000A4, w) && w == 5, "interp: endif body");
    Check(memory.Read32(0x800000A8, w) && w == 6, "interp: endif resumes");

    const auto range = ParseOne(
        "$Range\n"
        "CE000000 80009000\n"
        "04000100 00000001\n"
        "E0000000 00000000\n");
    Check(RunCode(range, memory).ok(), "interp: address range succeeds");
    Check(memory.Read32(0x80000100, w) && w == 1, "interp: address range writes");
}

void TestInterpreterBasePointerRegisters() {
    Gecko::FlatMemory memory(0x80000000u, 0x2000u);
    uint32_t w = 0;
    uint8_t b = 0;

    const auto base = ParseOne("$Base\n42000000 80000000\n04000100 00000007\n");
    Check(RunCode(base, memory).ok(), "interp: base set succeeds");
    Check(memory.Read32(0x80000100, w) && w == 7, "interp: base set address");

    const auto pointer = ParseOne("$Pointer\n4A000000 80000200\n14000000 00000008\n");
    Check(RunCode(pointer, memory).ok(), "interp: pointer set succeeds");
    Check(memory.Read32(0x80000200, w) && w == 8, "interp: pointer set address");

    const auto reg = ParseOne("$Register\n80000001 0000002A\n84000001 80000300\n");
    Check(RunCode(reg, memory).ok(), "interp: register set/store succeeds");
    Check(memory.Read8(0x80000300, b) && b == 0x2A, "interp: register store value");

    const auto op = ParseOne("$RegisterOp\n80000002 00000010\n86000002 00000005\n84000002 80000304\n");
    Check(RunCode(op, memory).ok(), "interp: register op succeeds");
    Check(memory.Read8(0x80000304, b) && b == 0x15, "interp: register op value");
}

void TestInterpreterCounterAndFlow() {
    Gecko::FlatMemory memory(0x80000000u, 0x2000u);
    uint32_t w = 0;
    uint8_t b = 0;

    const auto counter = ParseOne(
        "$Counter\n"
        "A8000030 00000004\n"
        "040000C0 00000009\n"
        "E0000000 00000000\n");
    Gecko::State counterState;
    counterState.Reset(counter);
    const auto rc = Gecko::Interpreter::Run(counter, memory, counterState);
    Check(rc.ok(), "interp: counter if succeeds");
    Check(memory.Read32(0x800000C0, w) && w == 9, "interp: counter if writes");

    const auto repeat = ParseOne(
        "$Repeat\n"
        "80000000 00000000\n"
        "60000002 00000000\n"
        "86000000 00000001\n"
        "62000000 00000000\n"
        "84000000 80000400\n");
    const auto rr = RunCode(repeat, memory);
    Check(rr.ok(), "interp: repeat succeeds");
    Check(memory.Read8(0x80000400, b) && b == 3, "interp: repeat count");

    const auto gotoLoop = ParseOne("$Loop\n6600FFFF 00000000\n");
    Gecko::Limits loopLimits;
    loopLimits.maxInstructions = 16;
    Gecko::State loopState;
    loopState.Reset(gotoLoop);
    const auto rl = Gecko::Interpreter::Run(gotoLoop, memory, loopState, loopLimits);
    Check(rl.status == Gecko::ExecutionStatus::ExecutionLimitReached, "interp: instruction budget");

    const auto repeatLimit = ParseOne("$RepeatLimit\n6000FFFF 00000000\n62000000 00000000\n");
    Gecko::Limits repeatLimits;
    repeatLimits.maxRepeatIterations = 8;
    Gecko::State repeatState;
    repeatState.Reset(repeatLimit);
    const auto rp = Gecko::Interpreter::Run(repeatLimit, memory, repeatState, repeatLimits);
    Check(rp.status == Gecko::ExecutionStatus::ExecutionLimitReached, "interp: repeat limit");

    const auto gosub = ParseOne(
        "$Gosub\n"
        "68000002 00000000\n"
        "040000E0 00000001\n"
        "66000002 00000000\n"
        "040000E8 00000003\n"
        "64000000 00000000\n"
        "040000EC 00000004\n");
    const auto rg = RunCode(gosub, memory);
    Check(rg.ok(), "interp: gosub/return succeeds");
    Check(memory.Read32(0x800000E0, w) && w == 1, "interp: gosub body");
    Check(memory.Read32(0x800000E8, w) && w == 3, "interp: gosub target");
    Check(memory.Read32(0x800000EC, w) && w == 4, "interp: gosub continues");

    const auto end = ParseOne(
        "$End\n"
        "F0000000 00000000\n"
        "040000F0 00000001\n");
    const auto re = RunCode(end, memory);
    Check(re.ok(), "interp: end of codes succeeds");
    Check(memory.Read32(0x800000F0, w) && w == 0, "interp: end of codes stops");
}

void TestInterpreterSafety() {
    Gecko::FlatMemory memory(0x80000000u, 0x1000u);

    const auto unaligned = ParseOne("$Unaligned\n02000001 00001234\n");
    Check(RunCode(unaligned, memory).status == Gecko::ExecutionStatus::MemoryAccessViolation, "interp: unaligned rejected");

    const auto outOfRange = ParseOne("$Out\n04001000 00000001\n");
    Check(RunCode(outOfRange, memory).status == Gecko::ExecutionStatus::MemoryAccessViolation, "interp: out of range rejected");

    const auto overflow = ParseOne("$Overflow\n42000000 FFFFFFFC\n04000004 00000001\n");
    Check(RunCode(overflow, memory).status == Gecko::ExecutionStatus::MemoryAccessViolation, "interp: address overflow rejected");

    const auto unsupported = ParseOne(
        "$ASM\n"
        "C2000000 00000001\n"
        "60000000 00000000\n"
        "00000000 00000000\n");
    Check(RunCode(unsupported, memory).status == Gecko::ExecutionStatus::UnsupportedInstruction, "interp: ASM rejected");

    const auto malformed = ParseOne("$Malformed\n08000000 00000000\n");
    Check(!malformed.valid, "interp: malformed code marked invalid");
    Check(RunCode(malformed, memory).status == Gecko::ExecutionStatus::InvalidInstruction, "interp: malformed rejected");

    Gecko::Limits accessLimits;
    accessLimits.maxMemoryAccesses = 4;
    const auto fill = ParseOne("$Fill\n00000040 000F00AA\n");
    Gecko::State fillState;
    fillState.Reset(fill);
    Check(Gecko::Interpreter::Run(fill, memory, fillState, accessLimits).status == Gecko::ExecutionStatus::ExecutionLimitReached, "interp: memory access limit");
}

void TestInterpreterSearch() {
    Gecko::FlatMemory memory(0x80000000u, 0x20000u);
    const uint32_t pattern = 0xDEADBEEFu;
    const uint32_t pattern2 = 0xCAFEBABEu;
    Check(memory.Write32(0x80003000, pattern), "interp: search pattern write");
    Check(memory.Write32(0x80003004, pattern2), "interp: search pattern write 2");

    const auto search = ParseOne(
        "$Search\n"
        "F6000001 80008001\n"
        "DEADBEEF CAFEBABE\n"
        "E2000001 00000000\n");
    Gecko::State state;
    state.Reset(search);
    const auto result = Gecko::Interpreter::Run(search, memory, state);
    Check(result.ok(), "interp: search succeeds");
    Check(state.pointer == 0x80003000u, "interp: search sets pointer");

    Gecko::Limits limits;
    limits.maxSearchIterations = 0;
    Gecko::State limitedState;
    limitedState.Reset(search);
    Check(Gecko::Interpreter::Run(search, memory, limitedState, limits).status == Gecko::ExecutionStatus::ExecutionLimitReached, "interp: search limit");
}

void TestInterpreterStateReset() {
    Gecko::FlatMemory memory(0x80000000u, 0x2000u);
    const auto code = ParseOne("$State\n42000000 80000100\n80000003 0000002A\n04000000 00000001\n");
    Gecko::State state;
    state.Reset(code);
    Check(state.base == 0x80000000u, "interp: reset base");
    Check(state.pointer == 0x80000000u, "interp: reset pointer");
    Check(state.registers[3] == 0, "interp: reset register");

    Check(Gecko::Interpreter::Run(code, memory, state).ok(), "interp: state run succeeds");
    Check(state.base == 0x80000100u, "interp: state base changed");
    Check(state.registers[3] == 0x2A, "interp: state register changed");

    state.Reset(code);
    Check(state.base == 0x80000000u, "interp: reset restores base");
    Check(state.registers[3] == 0, "interp: reset restores register");
}

} // namespace

int main() {
    TestOneValidCode();
    TestMultipleCodes();
    TestCommentsAndBlankLines();
    TestCrlfAndLf();
    TestInvalidHex();
    TestMissingWords();
    TestIncompleteSerial();
    TestUnknownCodetype();
    TestUnsupportedCodetype();
    TestExecuteAsm();
    TestSupportedCodetype();
    TestRegionMismatch();
    TestEmptyInput();
    TestLargeInput();
    TestMalformedInput();
    TestLimits();
    TestGameIdMetadata();
    TestDuplicateIds();
    TestStringWrite();
    TestSerial();
    TestCounterIf();
    TestSearch();
    TestInstructionOutsideCode();
    TestEmptyCodeName();
    TestNoInstructions();
    TestCompatibilityTable();
    TestInterpreterWrites();
    TestInterpreterConditions();
    TestInterpreterBasePointerRegisters();
    TestInterpreterCounterAndFlow();
    TestInterpreterSafety();
    TestInterpreterSearch();
    TestInterpreterStateReset();

    if (failures != 0) {
        std::cerr << failures << " Gecko check(s) failed\n";
        return 1;
    }

    std::cout << "Gecko parser/interpreter checks passed\n";
    return 0;
}
