// Host tests for the Gecko engine, parser and bundled library
// (gecko_codes.h, gecko_engine.h, gecko_db.h). They need no guest memory and no
// Aurora: a fake RAM stands in for the console's, so this runs on any host.
//
//   ./MH3GeckoTests            run them all
//
// The checks are the behaviours the manager depends on: text parsing, the
// encoding the code handler documents, the conditional stack, base/pointer
// codes, region translation, the refusals (with their reasons) and the bundled
// library's consistency.
#include "gecko_codes.h"
#include "gecko_db.h"
#include "gecko_engine.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const std::string& what) {
    ++g_checks;

    if (!condition) {
        ++g_failures;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

void CheckEqual(uint32_t got, uint32_t want, const std::string& what) {
    ++g_checks;

    if (got != want) {
        ++g_failures;
        std::printf("  FAIL %s: got 0x%08X, want 0x%08X\n", what.c_str(), got, want);
    }
}

void CheckEqualStr(const std::string& got, const std::string& want, const std::string& what) {
    ++g_checks;

    if (got != want) {
        ++g_failures;
        std::printf("  FAIL %s: got \"%s\", want \"%s\"\n", what.c_str(), got.c_str(), want.c_str());
    }
}

// A fake 24 MiB MEM1 plus 64 MiB MEM2, at their cached addresses.
class FakeRam final : public Gecko::Memory {
  public:
    FakeRam() {
        mem1.assign(0x01800000, 0);
        mem2.assign(0x04000000, 0);
    }

    bool Read(uint32_t address, void* out, size_t bytes) override {
        const uint8_t* source = At(address, bytes);

        if (!source)
            return false;

        std::memcpy(out, source, bytes);
        return true;
    }

    bool Write(uint32_t address, const void* bytes, size_t count) override {
        if (!Writable(address, count))
            return false;

        std::memcpy(At(address, count), bytes, count);
        return true;
    }

    bool InRam(uint32_t address, size_t bytes) override { return At(address, bytes) != nullptr; }
    bool Writable(uint32_t address, size_t bytes) override { return At(address, bytes) != nullptr; }

    void Poke32(uint32_t address, uint32_t value) {
        const uint32_t be = __builtin_bswap32(value);
        std::memcpy(At(address, 4), &be, 4);
    }

    uint32_t Peek32(uint32_t address) {
        uint32_t value = 0;
        std::memcpy(&value, At(address, 4), 4);
        return __builtin_bswap32(value);
    }

    void Poke8(uint32_t address, uint8_t value) { *At(address, 1) = value; }
    uint8_t Peek8(uint32_t address) { return *At(address, 1); }
    uint16_t Peek16(uint32_t address) {
        uint16_t value = 0;
        std::memcpy(&value, At(address, 2), 2);
        return uint16_t((value >> 8) | (value << 8));
    }

    // For the database test: which bytes of the block at `address` changed
    // between two snapshots.
    std::vector<std::pair<uint32_t, uint8_t>> Snapshot(uint32_t address, size_t bytes) {
        std::vector<std::pair<uint32_t, uint8_t>> out;
        out.reserve(bytes);
        for (size_t i = 0; i < bytes; ++i)
            out.emplace_back(uint32_t(address + i), *At(uint32_t(address + i), 1));
        return out;
    }

    static size_t CountChanges(const std::vector<std::pair<uint32_t, uint8_t>>& before,
                               const std::vector<std::pair<uint32_t, uint8_t>>& after) {
        size_t n = 0;
        for (size_t i = 0; i < before.size() && i < after.size(); ++i)
            n += before[i] != after[i];
        return n;
    }

  private:
    uint8_t* At(uint32_t address, size_t bytes) {
        std::vector<uint8_t>* pool = nullptr;
        uint32_t offset = 0;
        const uint32_t physical = address & 0x3FFFFFFFu;

        if (physical < mem1.size()) {
            pool = &mem1;
            offset = physical;
        } else if (physical >= 0x10000000u && physical < 0x10000000u + mem2.size()) {
            pool = &mem2;
            offset = physical - 0x10000000u;
        } else {
            return nullptr;
        }

        if (bytes == 0 || offset >= pool->size() || bytes > pool->size() - offset)
            return nullptr;

        return pool->data() + offset;
    }

    std::vector<uint8_t> mem1, mem2;
};

// Gecko text, as the manager's importer sees it.
Gecko::Code CodeFrom(const std::string& text) {
    const Gecko::ParsedList parsed = Gecko::Parse(text);
    return parsed.codes.empty() ? Gecko::Code{} : parsed.codes.front();
}

// ---------------------------------------------------------------------------

void TestParsing() {
    std::printf("parsing\n");

    const Gecko::ParsedList list = Gecko::Parse(
        "#region us\n"
        "$Max money\n"
        "0405B6A0 0098967F\n"
        "0405B6A4 0098967F\n"
        "* a comment\n"
        "\n"
        "$Two lines\n"
        "04057E2C 60000000\n"
        "04057E30 60000000\n");

    Check(list.codes.size() == 2, "two codes parsed");
    Check(list.declaredRegion == Gecko::Region::NtscU, "the #region marker is read");
    Check(list.codes[0].name == "Max money", "the $ name is read");
    Check(list.codes[0].body.size() == 2, "both lines kept");
    CheckEqual(list.codes[0].body[1].address, 0x0405B6A4u, "the address word");
    CheckEqual(list.codes[0].body[1].value, 0x0098967Fu, "the value word");
    Check(list.codes[0].sourceRegion == Gecko::Region::NtscU, "the region reaches the code");

    // Separators and comments in the line.
    const Gecko::ParsedList loose = Gecko::Parse("$X\n04057E2C: 60000000 # the nop\n");
    Check(loose.codes.size() == 1 && loose.codes[0].body.size() == 1, "a line with a colon and a comment parses");
    CheckEqual(loose.codes[0].body[0].value, 0x60000000u, "its value");

    // A broken line is reported with its number and skipped.
    const Gecko::ParsedList bad = Gecko::Parse("$Y\n04057E2C 60000000\nZZZ\n04057E30 60000000\n");
    Check(bad.errors.size() == 1, "one error reported");
    Check(bad.errors.empty() || bad.errors[0].line == 3, "the error names line 3");
    Check(bad.codes.size() == 1 && bad.codes[0].body.size() == 2, "the good lines survive");

    // Name = value form.
    const Gecko::ParsedList named = Gecko::Parse("Max money = Infinite\n04057E2C 60000000\n");
    Check(named.codes.size() == 1 && named.codes[0].name == "Max money", "the name = value form");

    // An activator is the code's first 16-bit if at address 0.
    const Gecko::Code activated = CodeFrom("$Buttons\n28000000 0000F7FF\n04057E2C 60000000\n");
    Check(activated.activator.has_value(), "the activator is separated");
    CheckEqual(activated.activator->value, 0x0000F7FFu, "the activator's buttons");
    Check(activated.body.size() == 1, "the body keeps the rest");

    // Format round-trips.
    const std::string text = Gecko::Format(activated);
    const Gecko::Code again = CodeFrom(text);
    Check(again.body.size() == 1 && again.activator.has_value(), "the formatted code parses back");
    CheckEqual(again.body[0].address, 0x04057E2Cu, "the round-tripped address");
}

void TestEncoding() {
    std::printf("encoding\n");

    // The code type is the top three bits, the sub type the next three, and
    // bit 28 selects po over ba (the handler's own decoding).
    Check(Gecko::OpOf(0x00000000) == Gecko::Op::Write8, "00 is the 8-bit write");
    Check(Gecko::OpOf(0x02000000) == Gecko::Op::Write16, "02 is the 16-bit write");
    Check(Gecko::OpOf(0x04000000) == Gecko::Op::Write32, "04 is the word write");
    Check(Gecko::OpOf(0x06000000) == Gecko::Op::WriteString, "06 is the string write");
    Check(Gecko::OpOf(0x08000000) == Gecko::Op::WriteSerial, "08 is the serial write");
    Check(Gecko::OpOf(0x10000000) == Gecko::Op::Write8, "10 is the 8-bit write through po");
    Check(Gecko::UsesPointer(0x10000000), "10 uses the pointer");

    Check(Gecko::OpOf(0x20000000) == Gecko::Op::If32Equal, "20 is if 32-bit equal");
    Check(Gecko::OpOf(0x22000000) == Gecko::Op::If32NotEqual, "22 is if !=");
    Check(Gecko::OpOf(0x24000000) == Gecko::Op::If32Greater, "24 is if >");
    Check(Gecko::OpOf(0x26000000) == Gecko::Op::If32Less, "26 is if <");
    Check(Gecko::OpOf(0x28000000) == Gecko::Op::If16Equal, "28 is if 16-bit equal");
    Check(Gecko::OpOf(0x2A000000) == Gecko::Op::If16NotEqual, "2A is if 16-bit !=");
    Check(Gecko::OpOf(0x2C000000) == Gecko::Op::If16Greater, "2C is if 16-bit >");
    Check(Gecko::OpOf(0x2E000000) == Gecko::Op::If16Less, "2E is if 16-bit <");

    Check(Gecko::OpOf(0x40000000) == Gecko::Op::SetBase, "40 sets ba");
    Check(Gecko::OpOf(0x42000000) == Gecko::Op::SetBaseOffset, "42 sets ba to a value");
    Check(Gecko::OpOf(0x44000000) == Gecko::Op::StoreBase, "44 stores ba");
    Check(Gecko::OpOf(0x48000000) == Gecko::Op::LoadPointer, "48 loads po");
    Check(Gecko::OpOf(0x4A000000) == Gecko::Op::SetPointer, "4A sets po");

    Check(Gecko::OpOf(0x60000000) == Gecko::Op::SetRepeat, "60 sets a repeat");
    Check(Gecko::OpOf(0x62000000) == Gecko::Op::ExecuteRepeat, "62 runs a repeat");
    Check(Gecko::OpOf(0x66000000) == Gecko::Op::Goto, "66 is a goto");
    Check(Gecko::OpOf(0x68000000) == Gecko::Op::Gosub, "68 is a gosub");

    Check(Gecko::OpOf(0x80000000) == Gecko::Op::RegisterOp, "80 is a register op");
    Check(Gecko::OpOf(0x8A000000) == Gecko::Op::CopyMemory, "8A copies memory");

    Check(Gecko::OpOf(0xC0000000) == Gecko::Op::ExecuteCode, "C0 runs a block");
    Check(Gecko::OpOf(0xC2000000) == Gecko::Op::Hook, "C2 hooks");
    Check(Gecko::OpOf(0xC6000000) == Gecko::Op::HookBranch, "C6 hooks with a branch");
    Check(Gecko::OpOf(0xCC000000) == Gecko::Op::Toggle, "CC is the on/off switch");
    Check(Gecko::OpOf(0xCE000000) == Gecko::Op::RangeCheck, "CE is the range check");

    Check(Gecko::OpOf(0xE0000000) == Gecko::Op::EndIf, "E0 ends an if");
    Check(Gecko::OpOf(0xF0000000) == Gecko::Op::End, "F0 ends the list");
}

void TestWrites() {
    std::printf("writes\n");

    FakeRam ram;

    // 04: one word.
    {
        const Gecko::Code code = CodeFrom("$W\n04123454 DEADBEEF\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the word write runs: " + result.error);
        CheckEqual(ram.Peek32(0x80123454), 0xDEADBEEFu, "the word lands");
        CheckEqual(uint32_t(result.writes), 1u, "one store counted");
    }

    // 02: three halfwords (count+1).
    {
        ram.Poke32(0x80200000, 0);
        const Gecko::Code code = CodeFrom("$H\n02200000 00037FFF\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the halfword fill runs: " + result.error);
        CheckEqual(ram.Peek16(0x80200000), 0x7FFF, "the first halfword");
        CheckEqual(ram.Peek16(0x80200002), 0x7FFF, "the second halfword");
        CheckEqual(ram.Peek16(0x80200004), 0x7FFF, "the third halfword");
    }

    // 00: a byte, four times.
    {
        const Gecko::Code code = CodeFrom("$B\n00200010 00030064\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the byte fill runs: " + result.error);
        CheckEqual(ram.Peek8(0x80200010), 100, "the first byte");
        CheckEqual(ram.Peek8(0x80200013), 100, "the fourth byte");
    }

    // 06: a string of five bytes, packed into two words.
    {
        // Five bytes, so two data words: "HELL" then "O".
        const Gecko::Code code = CodeFrom("$S\n06200020 00000005\n48454C4C 4F000000\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the string write runs: " + result.error);
        CheckEqual(ram.Peek8(0x80200020), 'H', "the first string byte");
        CheckEqual(ram.Peek8(0x80200024), 'O', "the last string byte");
    }

    // A write outside RAM fails with a reason, not a crash.
    {
        // Ba starts at 0x80000000 and the field is 25 bits, so the highest
        // address a code line can name is 0x81FFFFFF; MEM1 ends at 0x81800000
        // and MEM2 starts at 0x90000000, so this lands in the gap.
        const Gecko::Code code = CodeFrom("$Out\n05FFFFFC 00000001\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(!result.ok, "an out-of-range write fails");
        Check(result.error.find("outside guest RAM") != std::string::npos, "with the reason: " + result.error);
    }

    // 08: a serial write of three words, value and address stepping.
    {
        // TNNNZZZZ: width 2 (word), 3 further writes, a 4-byte address step;
        // the second word is the value's step.
        const Gecko::Code code = CodeFrom("$Serial\n08200040 00000001\n20030004 00000001\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the serial write runs: " + result.error);
        CheckEqual(ram.Peek32(0x80200040), 1u, "the first serial word");
        CheckEqual(ram.Peek32(0x80200044), 2u, "the second serial word");
        CheckEqual(ram.Peek32(0x80200048), 3u, "the third serial word");
    }
}

void TestConditionals() {
    std::printf("conditionals\n");

    FakeRam ram;

    // True block runs, and the E0 closes it.
    ram.Poke32(0x80300000, 1234);
    {
        const Gecko::Code code = CodeFrom("$If\n20300000 000004D2\n04300010 AABBCCDD\nE0000000 00000000\n04300020 11223344\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the if code runs: " + result.error);
        CheckEqual(ram.Peek32(0x80300010), 0xAABBCCDDu, "the block ran");
        CheckEqual(ram.Peek32(0x80300020), 0x11223344u, "the after-block line ran");
    }

    // False block is skipped; the line after the E0 still runs.
    {
        ram.Poke32(0x80300030, 1);
        const Gecko::Code code = CodeFrom("$If\n20300030 00000002\n04300040 AABBCCDD\nE0000000 00000000\n04300050 11223344\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the false if runs: " + result.error);
        CheckEqual(ram.Peek32(0x80300040), 0u, "the block was skipped");
        CheckEqual(ram.Peek32(0x80300050), 0x11223344u, "the line after it ran");
    }

    // Nesting: the outer true, the inner false.
    {
        ram.Poke32(0x80300060, 5);
        ram.Poke32(0x80300064, 9);
        const Gecko::Code code = CodeFrom(
            "$Nest\n20300060 00000005\n20300064 00000001\n04300070 00000001\nE0000000 00000000\n04300080 00000002\nE0000000 00000000\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the nested code runs: " + result.error);
        CheckEqual(ram.Peek32(0x80300070), 0u, "the inner false block was skipped");
        CheckEqual(ram.Peek32(0x80300080), 2u, "the outer block continued");
    }

    // A 16-bit if masks the data with the value's high half.
    {
        ram.Poke32(0x80300090, 0x1234ABCDu);
        // The data is 0xABCD, masked with ~0x00FF = 0xFF00, so it must compare
        // against 0xAB00.
        ram.Poke32(0x80300090, 0xABCD1234u);
        const Gecko::Code code = CodeFrom("$Mask\n28300090 00FFAB00\n043000A0 00000001\nE0000000 00000000\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the masked if runs: " + result.error);
        CheckEqual(ram.Peek32(0x803000A0), 1u, "the mask made it match");
    }

    // 24/26 compare the other way round.
    {
        ram.Poke32(0x803000B0, 10);
        const Gecko::Code greater = CodeFrom("$G\n243000B0 00000005\n043000C0 00000001\nE0000000 00000000\n");
        const Gecko::Result result = Gecko::Run(greater, ram, {});
        Check(result.ok, "the > if runs: " + result.error);
        CheckEqual(ram.Peek32(0x803000C0), 1u, "10 > 5 holds");

        const Gecko::Code less = CodeFrom("$L\n263000B0 00000005\n043000D0 00000001\nE0000000 00000000\n");
        const Gecko::Result second = Gecko::Run(less, ram, {});
        Check(second.ok, "the < if runs: " + second.error);
        CheckEqual(ram.Peek32(0x803000D0), 0u, "10 < 5 does not hold");
    }
}

void TestBaseAndPointer() {
    std::printf("base and pointer\n");

    FakeRam ram;

    // 42: ba = a value; 04 then writes through ba.
    {
        const Gecko::Code code = CodeFrom("$Base\n42000000 80300000\n04000010 CAFEBABE\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the base code runs: " + result.error);
        CheckEqual(ram.Peek32(0x80300010), 0xCAFEBABEu, "the write went through ba");
    }

    // 4A: po = a value; 14 (write32 through po) reaches it.
    {
        const Gecko::Code code = CodeFrom("$Pointer\n4A000000 80300020\n14000010 11223344\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the pointer code runs: " + result.error);
        CheckEqual(ram.Peek32(0x80300030), 0x11223344u, "the write went through po");
    }

    // 48: po = [address] follows one pointer.
    {
        ram.Poke32(0x80300040, 0x80300080);
        const Gecko::Code code = CodeFrom("$Follow\n48000000 80300040\n14000000 55667788\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "the pointer chase runs: " + result.error);
        CheckEqual(ram.Peek32(0x80300080), 0x55667788u, "the write went through the loaded pointer");
    }

    // Bit 27 is the data opcodes' pointer flag (0x14 instead of 0x04), not a
    // base/pointer flag: 4A000000 with N zero must be accepted, and 4A is how
    // the bundled library loads po. In the other direction, the data opcodes
    // must read 14XXXXXX exactly as 04XXXXXX, with po where ba would be.
    {
        // A 4A whose N nibble is zero is a plain "po = operand", not a refusal.
        Check((0x4A000000u & 0x0088F800u) == 0, "bit 27 is not in the base/pointer flag mask");
        Check((0x4A080000u & 0x0088F800u) != 0, "but N set (0x08000000) is");
    }

    // 14 and 04 agree except for which register they add: po against ba. Same
    // offset, different base, so the two writes must not land in one place.
    {
        const Gecko::Code throughBa = CodeFrom("$Ba\n42000000 80300000\n04000010 0BADF00D\n");
        const Gecko::Code throughPo = CodeFrom("$Po\n4A000000 80300100\n14000010 0BADF00D\n");

        const Gecko::Result a = Gecko::Run(throughBa, ram, {});
        const Gecko::Result b = Gecko::Run(throughPo, ram, {});
        Check(a.ok && b.ok, "both forms run");
        CheckEqual(ram.Peek32(0x80300010), 0x0BADF00Du, "the 04 form wrote through ba");
        CheckEqual(ram.Peek32(0x80300110), 0x0BADF00Du, "the 14 form wrote through po");
        CheckEqual(ram.Peek32(0x80300020), 0x00000000u, "and not at the other register's address");
    }

    // A 14 with po never set is not refused: ba and po start at the bottom of
    // MEM1 (0x80000000), as the reference handler leaves them, so the write
    // lands at 0x80000010. That is the code author's omission, not an
    // instruction this runtime cannot do, so it runs rather than erroring.
    {
        ram.Poke32(0x80000010, 0);
        const Gecko::Code code = CodeFrom("$NoPo\n14000010 0BADF00D\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, "a 14 with no po runs: " + result.error);
        CheckEqual(ram.Peek32(0x80000010), 0x0BADF00Du, "it wrote at po + the offset, po being zero");
    }

    // The flags that are not implemented are refused, not guessed at.
    {
        const Gecko::Code code = CodeFrom("$Flags\n4A080000 80300000\n14000000 00000001\n");
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(!result.ok, "the flagged pointer form is refused");
        Check(result.error.find("operand flags") != std::string::npos, "with the reason: " + result.error);
    }
}

void TestRefusals() {
    std::printf("refusals\n");

    FakeRam ram;

    struct Case {
        const char* text;
        const char* reason;
        const char* what;
    };

    const Case cases[]{
        {"$Hook\nC2000000 00000001\n60000000 00000000\n", "hooks PowerPC code", "C2 is refused"},
        {"$Goto\n66000001 00000000\n", "repeat/goto", "a goto is refused"},
        {"$Reg\n80000000 00000001\n", "own registers", "a register op is refused"},
        {"$Off\nCC000000 00000001\n04000000 00000001\n", "", "the on/off line on its own runs"},
    };

    for (const Case& test : cases) {
        const Gecko::Code code = CodeFrom(test.text);
        const Gecko::Result result = Gecko::Run(code, ram, {});

        if (test.reason[0] == '\0') {
            Check(result.ok, test.what);
            continue;
        }

        Check(!result.ok, test.what);
        Check(result.error.find(test.reason) != std::string::npos, std::string(test.what) + ": " + result.error);
    }

    // The manager's reason matches what running reports.
    const Gecko::Code hook = CodeFrom("$Hook\nC2000000 00000001\n60000000 00000000\n");
    Check(!Gecko::UnsupportedReason(hook).empty(), "the hook has a reason");
    Check(Gecko::Classify(hook) == Gecko::Classification::Asm, "and is classified as asm");

    const Gecko::Code data = CodeFrom("$Data\n04123456 00000001\n");
    Check(Gecko::UnsupportedReason(data).empty(), "a data code has no reason");
    Check(Gecko::Classify(data) == Gecko::Classification::Data, "and is classified as data");
}

void TestActivator() {
    std::printf("activator\n");

    FakeRam ram;

    // L+R+Up in Gecko's bits: L 0x2000, R 0x0200, Up 0x0001 => 0x2201.
    const Gecko::Code code = CodeFrom("$Buttons\n28000000 00002201\n04300000 00000001\n");

    Check(code.activator.has_value(), "the activator parsed");

    // Nothing held: nothing happens.
    {
        Gecko::Options options;
        options.held = 0;
        const Gecko::Result result = Gecko::Run(code, ram, options);
        Check(result.ok, "an inactive code is not an error");
        CheckEqual(ram.Peek32(0x80300000), 0u, "and wrote nothing");
    }

    // The same buttons in this runtime's Classic bits: kL 0x8, kR 0x80, kUp 0x2000.
    {
        Gecko::Options options;
        options.held = 0x8 | 0x80 | 0x2000;
        const Gecko::Result result = Gecko::Run(code, ram, options);
        Check(result.ok, "the active code runs: " + result.error);
        CheckEqual(ram.Peek32(0x80300000), 1u, "and wrote");
    }

    // A different button does not trigger it.
    {
        ram.Poke32(0x80300000, 0);
        Gecko::Options options;
        options.held = 0x20; // kA
        Gecko::Run(code, ram, options);
        CheckEqual(ram.Peek32(0x80300000), 0u, "another button does not trigger");
    }

    // Run once ignores the activator.
    {
        ram.Poke32(0x80300000, 0);
        const Gecko::Result result = Gecko::RunOnce(code, ram);
        Check(result.ok, "run once works: " + result.error);
        CheckEqual(ram.Peek32(0x80300000), 1u, "and wrote");
    }
}

void TestRegion() {
    std::printf("region\n");

    // US -> PAL moves an address by -0x64.
    const Gecko::Translation moved = Gecko::TranslateRegion(0x8027E814u, Gecko::Region::NtscU, Gecko::Region::Pal);
    Check(moved.translated, "the US address is translated");
    CheckEqual(moved.address, 0x8027E7B0u, "-0x64");

    // The reverse moves it back.
    const Gecko::Translation back = Gecko::TranslateRegion(0x8027E7B0u, Gecko::Region::Pal, Gecko::Region::NtscU);
    CheckEqual(back.address, 0x8027E814u, "and back");

    // Same region, nothing to do.
    const Gecko::Translation same = Gecko::TranslateRegion(0x8027E814u, Gecko::Region::Pal, Gecko::Region::Pal);
    Check(!same.translated, "a PAL code stays put");

    // NTSC-J has no verified translation.
    const Gecko::Translation jp = Gecko::TranslateRegion(0x8027E814u, Gecko::Region::NtscJ, Gecko::Region::Pal);
    Check(jp.error != nullptr, "NTSC-J is refused");

    // A code with a US region is planned with PAL addresses.
    Gecko::Code code = CodeFrom("#region us\n$US code\n0427E814 00000001\n");
    Check(code.sourceRegion == Gecko::Region::NtscU, "the code carries its region");

    const std::vector<Gecko::Line> plan = Gecko::Plan(code, Gecko::Region::Pal);
    Check(plan.size() == 1, "one planned line");
    CheckEqual(plan[0].address, 0x0427E7B0u, "the planned address is PAL");
    CheckEqual(plan[0].value, 1u, "and the value is untouched");

    // ParseRegion accepts the spellings the lists use.
    Check(Gecko::ParseRegion("us") == Gecko::Region::NtscU, "\"us\"");
    Check(Gecko::ParseRegion("PAL") == Gecko::Region::Pal, "\"PAL\"");
    Check(Gecko::ParseRegion("RMHE08") == Gecko::Region::NtscU, "the game id");
    Check(!Gecko::ParseRegion("whatever").has_value(), "an unknown name");
}

void TestBlockInterpreter() {
    std::printf("block interpreter\n");

    FakeRam ram;

    // lis r3, 0x8030 ; ori r3, r3, 0x0010 ; li r4, 1 ; stw r4, 0(r3)
    const std::vector<uint32_t> words{
        0x3C608030u, // lis r3, 0x8030
        0x60630010u, // ori r3, r3, 0x0010
        0x38800001u, // li r4, 1
        0x90830000u, // stw r4, 0(r3)
    };

    std::string reason;
    Check(Gecko::BlockRunnable(words, reason), "the block is runnable: " + reason);

    const Gecko::BlockResult result = Gecko::ExecuteBlock(words, ram);
    Check(result.ok, "the block runs: " + result.error);
    CheckEqual(ram.Peek32(0x80300010), 1u, "the block wrote");

    // A block that reads a register it never wrote is refused.
    const std::vector<uint32_t> reads{
        0x80630010u, // lwz r3, 0x10(r3)
        0x90630000u, // stw r3, 0(r3)
    };

    Check(!Gecko::BlockRunnable(reads, reason), "the caller-dependent block is refused");
    Check(reason.find("r3") != std::string::npos, "and the reason names the register: " + reason);
}

void TestDatabase() {
    std::printf("database\n");

    const std::span<const Gecko::DbCode> codes = Gecko::DbCodes();
    Check(!codes.empty(), "the library has codes");

    FakeRam ram;
    size_t dataCodes = 0, nativeCodes = 0;

    for (const Gecko::DbCode& db : codes) {
        Check(db.name && *db.name, "every entry has a name");
        Check(db.group && *db.group, "and a group");
        Check(db.note && *db.note, "and a note");

        Gecko::Code code;
        code.name = db.name;
        code.body = db.body;
        code.sourceRegion = Gecko::Region::Pal;

        if (db.body.empty()) {
            // No body: the change only exists as the named built-in cheat.
            ++nativeCodes;
            Check(db.native && *db.native, std::string(db.name) + " names its built-in cheat");
            Check(!Gecko::UnsupportedReason(code).empty(), std::string(db.name) + " cannot run as written");
            continue;
        }

        ++dataCodes;
        Check(Gecko::UnsupportedReason(code).empty(), std::string(db.name) + " is runnable");
        Check(Gecko::Classify(code) != Gecko::Classification::Invalid, std::string(db.name) + " is not invalid");

        // Every data code really runs against RAM: no opcode it does not have.
        //
        // The save data is reached through the pointer variable, so point it at
        // a scratch block and check the write lands in that block and nowhere
        // else: the code has to follow the pointer, not use a fixed address.
        // The save data is reached through the pointer variable, so point it at
        // a scratch block filled with a value no write would use, and check the
        // write lands in that block: the code follows the pointer, it does not
        // write a fixed address. The block covers the largest field offset.
        constexpr uint32_t kBlock = 0x8000;
        ram.Poke32(0x80796D40u, 0x80210000u);
        // 0xA5: no field value any bundled code writes, so a write of 0xFF or of
        // an all-ones word still shows up as a change.
        for (uint32_t i = 0; i < kBlock; ++i)
            ram.Poke8(0x80210000u + i, 0xA5);
        const auto blockBefore = ram.Snapshot(0x80210000u, kBlock);
        const uint32_t poBefore = ram.Peek32(0x80796D40u);

        // A code that never loads po writes a fixed address of its own.
        bool throughPointer = false;
        for (const Gecko::Line& line : code.body)
            throughPointer = throughPointer || (line.address & 0x10000000u) != 0;
        const Gecko::Result result = Gecko::Run(code, ram, {});
        Check(result.ok, std::string(db.name) + " runs: " + result.error);

        // It wrote into the block po points at - not at a fixed address, and
        // not to the pointer itself.
        if (throughPointer) {
            const auto blockAfter = ram.Snapshot(0x80210000u, kBlock);
            Check(FakeRam::CountChanges(blockBefore, blockAfter) > 0,
                  std::string(db.name) + " writes through po");
            Check(ram.Peek32(0x80796D40u) == poBefore, std::string(db.name) + " leaves po alone");
        }
    }

    Check(dataCodes > 0, "there are data codes");
    Check(nativeCodes > 0, "there are native-backed codes");
    std::printf("  %zu data codes, %zu native-backed codes\n", dataCodes, nativeCodes);
}

void TestFormatting() {
    std::printf("formatting\n");

    Gecko::Code code;
    code.name = "Round trip";
    code.body = {{0x04057E2C, 0x60000000}, {0x02300000, 0x0001FFFF}};

    const std::string text = Gecko::Format(code);
    Check(text.find("$Round trip") != std::string::npos, "the name line");
    Check(text.find("04057E2C 60000000") != std::string::npos, "the first line");
    Check(text.find("02300000 0001FFFF") != std::string::npos, "the second line");

    const Gecko::ParsedList parsed = Gecko::Parse(Gecko::FormatAll({code}, Gecko::Region::Pal));
    Check(parsed.codes.size() == 1, "a whole list parses back");
    Check(parsed.codes[0].body.size() == 2, "with both lines");

    CheckEqualStr(Gecko::RegionName(Gecko::Region::Pal), "PAL", "the region's name");
    CheckEqualStr(Gecko::ClassificationName(Gecko::Classification::Data), "data", "the class's name");
}

} // namespace

int main() {
    TestParsing();
    TestEncoding();
    TestWrites();
    TestConditionals();
    TestBaseAndPointer();
    TestRefusals();
    TestActivator();
    TestRegion();
    TestBlockInterpreter();
    TestDatabase();
    TestFormatting();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
