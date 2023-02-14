# funycode - Unicode encoding for C symbol names

Like [Punycode](https://www.rfc-editor.org/rfc/rfc3492.html) (it's namesake and inspiration), funycode maps an input string in an extended alphabet to an output string in a more limited alphabet. The output alphabet used by funycode consists of all 7-bit ASCII characters that are valid for ANSI/ISO C symbol names:

    0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_

A funycode-encoded string consist of a *prefix* and a *suffix*, separated by an underscore (`_`), one of which must always be present. The prefix contains all direct-mapped characters in the input string (all characters in the alphabet except underscore), the suffix a sequence of variable-length encoded step counts for the decoder's state machine (see Punycode description for an explanation as to how this works).

| Original | Encoded | Remarks |
| --- | --- | --- |
| `foo` | `foo` | Prefix only. |
| `føø` | `f_670` | Prefix and suffix. |
| `𝓯𝓸𝓸` | `cxr0I00_` | Suffix only. |

The position of the separator and the encoding of the suffix are both chosen in a way as to always result in a valid non-reserved C identifier; specifically, the output will never start with an underscore or a digit.

## Compression

Symbol names for modern programming languages typically contain a lot of redundancy: not only as the names of parameter types, but also in the form of deeply-nested namespaces. When encoding these symbol names their length tends to be come unwieldy. Therefore, a simple compression algorithm is a mandatory part of funycode.

Consider a symbol such as the following 261-character one (from OpenBSD 7.1's `/usr/lib/libc++.a`):

    std::__1::__fs::filesystem::__last_write_time(std::__1::__fs::filesystem::path const&, std::__1::chrono::time_point<std::__1::__fs::filesystem::_FilesystemClock, std::__1::chrono::duration<__int128, std::__1::ratio<1ll, 1000000000ll> > >, std::__1::error_code*)

Without compression, this would translate to the following 278-character long funycode string:

    std1fsfilesystemlastwritetimestd1fsfilesystempathconststd1chronotimepointstd1fsfilesystemFilesystemClockstd1chronodurationint128std1ratio1ll1000000000llstd1errorcode_n05oOCC00xw3M7pAf5vAp0PDFxsI01020A0H01020A0G01060C01020A0K01060J010T010xSc1Lvg11rrx1030G045A030Y0FB030GM0K0D0c08

With compression however the result is this large but manageable 176-character string:

    std1fsfilesystemlastwritetimepathconstchronopointFClockdurationint1281ll10llerrorcode_X05Y40sjb4l6z2z7Zrr7030h0BzEF6rH11vwT0J5Q6G0Qqrwrzx0Wt1nuGEr4WrFln1ouLQp29SmzE8LSty1Mu5Ph6

Typically, for longer strings compression results in output that is 80-90% of the input; without compression that would be around 110%.

The compression algorithm used is based on [LZRW1-A](http://www.ross.net/compression/lzrw1a.html). Internally, matches are encoded as symbols in the `0xd800`-`0xdfff` range, with a 4-bit length and 7-bit distance.

## Examples

| Original | Encoded |
| -------- | ------- |
| `foo` | `foo` |
| `foo_bar` | `foobar_H7` |
| `supercalifragilisticexpialidocious` | `supercalifragilisticexpialidocious` |
| `bücher` | `bcher_eL` |
| `hörbücher` | `hrbcher_5S0u0` |
| `_` | `C1_` |
| (space) | `A0_` |
| `自転車` | `qeE4K2A1_` |
| `велосипед` | `FH420EHL9G_` |
| `wikipedia::article::wikilink::wikilink(std::string const&)` | `wikipediaarticlelinkstdstringconst_T0zGw0s0sw007080sywurJ3t1` |
| `<mycrate::Foo<u32> as mycrate::Bar<u64>>::foo` | `mycrateFoou32asBaru64foo_D02qs10G0ZCAy0B0sqzxxE` |
