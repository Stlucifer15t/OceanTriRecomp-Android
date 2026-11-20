#include "gecko/gecko_parser.h"

#include "gecko/gecko_compat.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Gecko {
namespace {

struct Line {
    int number = 0;
    std::string_view text;
};

struct WordLine {
    int number = 0;
    uint32_t addressWord = 0;
    uint32_t valueWord = 0;
};

std::vector<Line> SplitLines(std::string_view text) {
    std::vector<Line> lines;
    size_t start = 0;
    int number = 1;

    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        std::string_view line = end == std::string_view::npos ? text.substr(start) : text.substr(start, end - start);

        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        lines.push_back({number++, line});

        if (end == std::string_view::npos)
            break;

        start = end + 1;
    }

    return lines;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);

    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);

    return text;
}

bool IsComment(std::string_view text) {
    return text.starts_with("*") || text.starts_with("#") || text.starts_with("//");
}

bool IsGameIdToken(std::string_view text) {
    if (text.size() != 6)
        return false;

    for (char c : text)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            return false;

    return true;
}

bool ParseWord(std::string_view token, uint32_t& value) {
    if (token.size() != 8)
        return false;

    uint32_t word = 0;

    for (char c : token) {
        uint32_t digit;

        if (c >= '0' && c <= '9')
            digit = uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = uint32_t(c - 'A' + 10);
        else
            return false;

        word = (word << 4) | digit;
    }

    value = word;
    return true;
}

std::vector<std::string_view> Words(std::string_view text) {
    std::vector<std::string_view> words;
    size_t i = 0;

    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;

        if (i >= text.size())
            break;

        const size_t start = i;

        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;

        words.push_back(text.substr(start, i - start));
    }

    return words;
}

bool EqualIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;

    return true;
}

std::string FirstWord(std::string_view text) {
    size_t end = 0;

    while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])))
        ++end;

    return std::string(text.substr(0, end));
}

std::string ExtractGameId(std::string_view line) {
    const std::string_view text = Trim(line);

    if (text.size() >= 8 && text.front() == '[' && text.back() == ']') {
        const std::string_view inner = Trim(text.substr(1, text.size() - 2));

        if (IsGameIdToken(inner))
            return std::string(inner);
    }

    for (const std::string_view prefix : {"game id:", "game:", "id:", "disc:", "serial:"}) {
        if (text.size() >= prefix.size() && EqualIgnoreCase(text.substr(0, prefix.size()), prefix)) {
            const std::string token = FirstWord(Trim(text.substr(prefix.size())));

            if (IsGameIdToken(token))
                return token;
        }
    }

    if (IsGameIdToken(text))
        return std::string(text);

    const std::string first = FirstWord(text);

    if (IsGameIdToken(first))
        return first;

    return {};
}

uint64_t Fnv1a(std::string_view bytes, uint64_t hash) {
    for (const unsigned char c : bytes) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }

    return hash;
}

uint64_t CodeId(std::string_view name, const std::vector<Instruction>& instructions) {
    uint64_t hash = 0xcbf29ce484222325ull;
    hash = Fnv1a(name, hash);

    for (const Instruction& instruction : instructions) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash ^= (instruction.addressWord >> shift) & 0xFFu;
            hash *= 0x100000001b3ull;
        }

        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash ^= (instruction.valueWord >> shift) & 0xFFu;
            hash *= 0x100000001b3ull;
        }
    }

    return hash;
}

std::string HexByte(uint8_t value) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    std::string text;
    text += kDigits[value >> 4];
    text += kDigits[value & 0xFu];
    return text;
}

bool HasError(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

} // namespace

ParseResult Parse(std::string_view text, const ParseOptions& options) {
    ParseResult result;

    if (text.size() > options.maxTextBytes) {
        result.diagnostics.push_back({DiagnosticSeverity::Error, 0, {}, "input exceeds the maximum size"});
        return result;
    }

    const std::vector<Line> lines = SplitLines(text);

    size_t firstCode = lines.size();

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view trimmed = Trim(lines[i].text);

        if (!trimmed.empty() && trimmed.front() == '$') {
            firstCode = i;
            break;
        }
    }

    std::string header;

    for (size_t i = 0; i < firstCode; ++i) {
        if (!header.empty())
            header += '\n';

        header += lines[i].text;

        const std::string_view trimmed = Trim(lines[i].text);

        if (trimmed.empty() || IsComment(trimmed))
            continue;

        const std::string gameId = ExtractGameId(trimmed);

        if (!gameId.empty()) {
            if (result.gameId.empty())
                result.gameId = gameId;

            continue;
        }

        const std::vector<std::string_view> words = Words(trimmed);
        uint32_t addressWord = 0, valueWord = 0;

        if (words.size() == 2 && ParseWord(words[0], addressWord) && ParseWord(words[1], valueWord)) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, lines[i].number, {}, "instruction outside a code"});
        } else {
            result.diagnostics.push_back({DiagnosticSeverity::Warning, lines[i].number, {}, "text before the first code is not a game ID or comment"});
        }
    }

    result.headerText = std::move(header);
    result.region = RegionFromGameId(result.gameId);

    if (firstCode == lines.size()) {
        result.diagnostics.push_back({DiagnosticSeverity::Warning, 0, {}, "no Gecko codes found"});
        return result;
    }

    size_t i = firstCode;

    while (i < lines.size()) {
        if (result.codes.size() >= options.maxCodes) {
            result.diagnostics.push_back({DiagnosticSeverity::Error, lines[i].number, {}, "too many codes"});
            break;
        }

        const std::string_view headerLine = Trim(lines[i].text);

        if (headerLine.empty() || headerLine.front() != '$') {
            ++i;
            continue;
        }

        Code code;
        code.name = std::string(Trim(headerLine.substr(1)));

        if (code.name.empty())
            code.diagnostics.push_back({DiagnosticSeverity::Warning, lines[i].number, {}, "code has an empty name"});

        const size_t blockStart = i;
        ++i;

        std::vector<Line> body;

        while (i < lines.size()) {
            const std::string_view trimmed = Trim(lines[i].text);

            if (!trimmed.empty() && trimmed.front() == '$')
                break;

            body.push_back(lines[i]);
            ++i;
        }

        std::string source;

        for (size_t k = blockStart; k < i; ++k) {
            if (!source.empty())
                source += '\n';

            source += lines[k].text;
        }

        code.sourceText = std::move(source);

        std::vector<WordLine> wordLines;

        for (const Line& line : body) {
            const std::string_view trimmed = Trim(line.text);

            if (trimmed.empty())
                continue;

            if (IsComment(trimmed)) {
                code.comments.emplace_back(trimmed);
                continue;
            }

            const std::vector<std::string_view> words = Words(trimmed);

            if (words.size() != 2) {
                code.diagnostics.push_back({DiagnosticSeverity::Error, line.number, code.name, "expected two hexadecimal words"});
                continue;
            }

            uint32_t addressWord = 0, valueWord = 0;

            if (!ParseWord(words[0], addressWord) || !ParseWord(words[1], valueWord)) {
                code.diagnostics.push_back({DiagnosticSeverity::Error, line.number, code.name, "invalid hexadecimal word (expected 8 digits)"});
                continue;
            }

            wordLines.push_back({line.number, addressWord, valueWord});
        }

        for (size_t k = 0; k < code.comments.size(); ++k) {
            if (k)
                code.description += '\n';

            code.description += code.comments[k];
        }

        const bool tooManyInstructions = wordLines.size() > options.maxInstructionsPerCode;

        if (tooManyInstructions)
            code.diagnostics.push_back({DiagnosticSeverity::Error, lines[blockStart].number, code.name, "too many instructions in code"});

        if (wordLines.empty()) {
            code.diagnostics.push_back({DiagnosticSeverity::Error, lines[blockStart].number, code.name, "code has no instructions"});
        } else if (tooManyInstructions) {
            code.support = SupportLevel::Unknown;
        } else {
            bool valid = true;
            SupportLevel support = SupportLevel::Supported;
            size_t w = 0;

            while (w < wordLines.size()) {
                const WordLine& first = wordLines[w];
                const uint8_t type = uint8_t((first.addressWord >> 25) & 0x7Fu);
                const uint32_t address = (((first.addressWord >> 24) & 1u) << 24) | (first.addressWord & 0x00FFFFFFu);
                const CodetypeInfo info = ClassifyCodetype(type);
                const CodetypeLength length = InstructionLength(type, first.addressWord, first.valueWord);

                if (!length.valid) {
                    code.diagnostics.push_back({DiagnosticSeverity::Error, first.number, code.name, length.error ? length.error : "invalid codetype length"});
                    valid = false;
                    break;
                }

                if (length.lines > wordLines.size() - w) {
                    code.diagnostics.push_back({DiagnosticSeverity::Error, first.number, code.name, "incomplete codetype block"});
                    valid = false;
                    break;
                }

                if (info.support == SupportLevel::Unsupported) {
                    code.diagnostics.push_back({DiagnosticSeverity::Warning, first.number, code.name, "unsupported Gecko codetype: " + std::string(info.name)});
                } else if (info.support == SupportLevel::RecognizedNotImplemented) {
                    code.diagnostics.push_back({DiagnosticSeverity::Warning, first.number, code.name, "codetype not implemented yet: " + std::string(info.name)});
                } else if (info.support == SupportLevel::Unknown) {
                    code.diagnostics.push_back({DiagnosticSeverity::Warning, first.number, code.name, "unknown Gecko codetype 0x" + HexByte(type)});
                }

                if (type == uint8_t(Codetype::ExecuteAsm)) {
                    const uint32_t last = wordLines[w + length.lines - 1].addressWord;

                    if (last != 0x4E800020u && last != 0x00000000u)
                        code.diagnostics.push_back({DiagnosticSeverity::Warning, wordLines[w + length.lines - 1].number, code.name, "C0 block does not end with blr"});
                } else if (type == uint8_t(Codetype::InsertAsm) || type == uint8_t(Codetype::InsertAsmPo) ||
                           type == uint8_t(Codetype::InsertAsmChecksum) || type == uint8_t(Codetype::InsertAsmChecksumPo)) {
                    const uint32_t last = wordLines[w + length.lines - 1].addressWord;

                    if (last != 0x00000000u)
                        code.diagnostics.push_back({DiagnosticSeverity::Warning, wordLines[w + length.lines - 1].number, code.name, "ASM block does not end with a 00000000 terminator"});
                }

                Instruction instruction;
                instruction.addressWord = first.addressWord;
                instruction.valueWord = first.valueWord;
                instruction.codetype = type;
                instruction.address = address;

                for (size_t k = 1; k < length.lines; ++k) {
                    instruction.extraWords.push_back(wordLines[w + k].addressWord);
                    instruction.extraWords.push_back(wordLines[w + k].valueWord);
                }

                code.instructions.push_back(std::move(instruction));
                support = CombineSupport(support, info.support);
                w += length.lines;
            }

            if (w < wordLines.size()) {
                code.diagnostics.push_back({DiagnosticSeverity::Error, wordLines[w].number, code.name, "unexpected extra instruction word"});
                valid = false;
            }

            code.support = support;
            code.valid = valid && !HasError(code.diagnostics);
        }

        code.gameId = result.gameId;
        code.region = result.region;
        code.gameCompatibility = ClassifyGameCompatibility(code.gameId, options.targetGameId);
        code.id = CodeId(code.name, code.instructions);

        for (const Code& other : result.codes) {
            if (other.id == code.id) {
                code.diagnostics.push_back({DiagnosticSeverity::Warning, lines[blockStart].number, code.name, "duplicate code id"});
                break;
            }
        }

        result.codes.push_back(std::move(code));
    }

    return result;
}

} // namespace Gecko
