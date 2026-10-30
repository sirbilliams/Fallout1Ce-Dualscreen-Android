#ifndef FALLOUT_MUI_CHARSET_H_
#define FALLOUT_MUI_CHARSET_H_

namespace fallout {

// Fallout 1's interface fonts are indexed by Latin-1 bytes.
inline char32_t muiCharsetDecode(unsigned char ch) { return ch; }

inline unsigned char muiCharsetEncode(char32_t cp)
{
    if (cp < 0x100) return static_cast<unsigned char>(cp);
    switch (cp) {
    case 0x2018:
    case 0x2019:
        return '\'';
    case 0x201C:
    case 0x201D:
        return '"';
    case 0x2013:
    case 0x2014:
        return '-';
    }
    return 0;
}

} // namespace fallout

#endif
