// miniz is statically linked; the player needs no extra runtime DLL.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_STDIO
#include "../third_party/miniz/miniz.c"
#include "../third_party/miniz/miniz_tdef.c"
#include "../third_party/miniz/miniz_tinfl.c"
#include "../third_party/miniz/miniz_zip.c"
