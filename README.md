# funycode - Unicode encoding for C symbol names

Like [Punycode](https://www.rfc-editor.org/rfc/rfc3492.html) (it's namesake and inspiration), funycode maps an input string in an extended alphabet to an output string in a more limited alphabet. The output alphabet used by funycode consists of all 7-bit ASCII characters that are valid for ANSI/ISO C symbol names:

    0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_

A funycode-encoded string consist of a *prefix* and a *suffix*, separated by an underscore (`_`), one of which must always be present. The prefix contains all direct-mapped characters in the input string (all characters in the alphabet except underscore), the suffix a sequence of variable-length encoded step counts for the decoder's state machine (see Punycode description for an explanation as to how this works).

| Original | Encoded | Remarks |
| --- | --- | --- |
| `foo` | `foo` | Prefix only. |
| `føø` | `f_b80` | Prefix and suffix. |
| `𝓯𝓸𝓸` | `vysEI00_` | Suffix only. |

The position of the separator and the encoding of the suffix are both chosen in a way as to always result in a valid non-reserved C identifier; specifically, the output will never start with an underscore or a digit.

## Compression

Symbol names for modern programming languages typically contain a lot of redundancy: not only as the names of parameter types, but also in the form of deeply-nested namespaces. When encoding these symbol names their length tends to be come unwieldy. Therefore, a simple compression algorithm is a mandatory part of funycode.

Consider a symbol such as the following 261-character one (from OpenBSD 7.1's `/usr/lib/libc++.a`):

    std::__1::__fs::filesystem::__last_write_time(std::__1::__fs::filesystem::path const&, std::__1::chrono::time_point<std::__1::__fs::filesystem::_FilesystemClock, std::__1::chrono::duration<__int128, std::__1::ratio<1ll, 1000000000ll> > >, std::__1::error_code*)

Without compression, this would translate to the following 315-character long funycode string:

    std1fsfilesystemlastwritetimestd1fsfilesystempathconststd1chronotimepointstd1fsfilesystemFilesystemClockstd1chronodurationint128std1ratio1ll1000000000llstd1errorcode_zu650o0O0C0C000dFN5L5u2v3p0P0D0F0xd010200A00H0010200A00G0010600C0010200A00K0010600J0010T0010d551L0v711hs70300G004050A00300Y00F0B00300G0M00K00D00c0080

With compression however the result is this large but manageable 196-character string:

    std1fsfilesystemlastwritetimepathconstschronopointFilClockdurationint1281ll10llerrorcode_pq150b0400d8x2f2a1L2c02M0300i00D0P3H060n311Xy00J050R0600I00Q02yztv5Oq7YrchtPov2mvMTr5DyEnLxys0frbwwK7w9Byz4

Typically, compression results in output strings that are around 98% of their unencoded equivalents; without compression that would be around 125%.

The compression algorithm used is based on [LZRW1-A](http://www.ross.net/compression/lzrw1a.html). Internally, matches are encoded as symbols in the `0xd800`-`0xdfff` range, with a 4-bit length and 7-bit distance.

## Examples

| Original | Encoded |
| -------- | ------- |
| `foo` | `foo` |
| `foo_bar` | `foobar_IC` |
| `supercalifragilisticexpialidocious` | `supercalifragilisticexpialidocious` |
| `bücher` | `bcher_DQ` |
| `hörbücher` | `hrbcher_9Yu0` |
| `_` | `j1_` |
| (space) | `g0_` |
| `自転車` | `Ssoqx5B1_` |
| `велосипед` | `4I40200E0H0L090G0_` |
| `wikipedia::article::wikilink::wikilink(std::string const&)` | `wikipediaarticlelinkstdstringconst_xOf3w0s0YA0700800sywxZxv6` |
| `<mycrate::Foo<u32> as mycrate::Bar<u64>>::foo` | `mycrateFoou32asBaru64foo_hH20WB0G00Z0C0A0y0B00EzyuS` |

