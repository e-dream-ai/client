#ifndef CLIENT_COMMON_UTF8_H_
#define CLIENT_COMMON_UTF8_H_

#include <cstdint>
#include <cstddef>
#include <string>

// UTF-8 helpers for HUD / cross-platform text.
// Strings are treated as UTF-8; invalid bytes decode to U+003F.

inline bool Utf8DecodeNext(const std::string& s, size_t& i, uint32_t& outCp)
{
    if (i >= s.size())
        return false;
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80u)
    {
        outCp = b0;
        ++i;
        return true;
    }
    if ((b0 & 0xE0u) == 0xC0u && i + 1 < s.size())
    {
        const unsigned b1 = static_cast<unsigned char>(s[i + 1]);
        outCp = (uint32_t(b0 & 0x1Fu) << 6) | uint32_t(b1 & 0x3Fu);
        if (outCp >= 0x80u)
        {
            i += 2;
            return true;
        }
    }
    else if ((b0 & 0xF0u) == 0xE0u && i + 2 < s.size())
    {
        const unsigned b1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned b2 = static_cast<unsigned char>(s[i + 2]);
        outCp = (uint32_t(b0 & 0x0Fu) << 12) | (uint32_t(b1 & 0x3Fu) << 6) | uint32_t(b2 & 0x3Fu);
        if (outCp >= 0x800u && !(outCp >= 0xD800u && outCp <= 0xDFFFu))
        {
            i += 3;
            return true;
        }
    }
    else if ((b0 & 0xF8u) == 0xF0u && i + 3 < s.size())
    {
        const unsigned b1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned b2 = static_cast<unsigned char>(s[i + 2]);
        const unsigned b3 = static_cast<unsigned char>(s[i + 3]);
        outCp = (uint32_t(b0 & 0x07u) << 18) | (uint32_t(b1 & 0x3Fu) << 12) |
                (uint32_t(b2 & 0x3Fu) << 6) | uint32_t(b3 & 0x3Fu);
        if (outCp >= 0x10000u && outCp <= 0x10FFFFu)
        {
            i += 4;
            return true;
        }
    }
    outCp = '?';
    ++i;
    return true;
}

/// Word-wrap UTF-8 without splitting code units. Breaks on last ASCII space in the run;
/// otherwise hard-breaks at the last code point that fits within maxBytesPerLine (byte count).
inline std::string Utf8WrapLinesByByteLimit(const std::string& msg, size_t maxBytesPerLine)
{
    if (maxBytesPerLine == 0)
        return msg;

    std::string out;
    out.reserve(msg.size() + msg.size() / (maxBytesPerLine ? maxBytesPerLine : 1) + 4);

    size_t lineStart = 0;
    const size_t n = msg.size();

    while (lineStart < n)
    {
        if (msg[lineStart] == '\r')
        {
            ++lineStart;
            continue;
        }
        if (msg[lineStart] == '\n')
        {
            out.push_back('\n');
            ++lineStart;
            continue;
        }

        size_t pos = lineStart;
        size_t lineUsed = 0;
        size_t lastSpaceAfter = std::string::npos;

        while (pos < n)
        {
            if (msg[pos] == '\r')
            {
                ++pos;
                continue;
            }
            if (msg[pos] == '\n')
            {
                out.append(msg, lineStart, pos - lineStart);
                out.push_back('\n');
                lineStart = pos + 1;
                goto continue_outer;
            }

            const size_t cpStart = pos;
            uint32_t cp = 0;
            Utf8DecodeNext(msg, pos, cp);
            const size_t cpLen = pos - cpStart;

            if (lineUsed + cpLen > maxBytesPerLine && cpStart > lineStart)
            {
                if (lastSpaceAfter != std::string::npos && lastSpaceAfter > lineStart)
                {
                    out.append(msg, lineStart, lastSpaceAfter - lineStart);
                    out.push_back('\n');
                    lineStart = lastSpaceAfter;
                    while (lineStart < n && msg[lineStart] == ' ')
                        ++lineStart;
                }
                else
                {
                    out.append(msg, lineStart, cpStart - lineStart);
                    out.push_back('\n');
                    lineStart = cpStart;
                }
                goto continue_outer;
            }

            if (lineUsed + cpLen > maxBytesPerLine && cpStart == lineStart)
            {
                out.append(msg, cpStart, cpLen);
                lineStart = pos;
                if (lineStart < n && msg[lineStart] != '\n' && msg[lineStart] != '\r')
                    out.push_back('\n');
                goto continue_outer;
            }

            lineUsed += cpLen;
            if (cp == ' ' || cp == '\t')
                lastSpaceAfter = pos;
        }

        out.append(msg, lineStart, pos - lineStart);
        lineStart = pos;

    continue_outer:;
    }

    return out;
}

#endif
