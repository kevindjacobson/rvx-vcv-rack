#!/bin/sh
set -eu

commit=71351d4b484cd2d1917867f7846a5cdca724552d
archive_sha256=9196aceb663bc87a3d9981fb993135cafb23a7df7a832d4cac5138925b6bbb1d
archive_url="https://codeload.github.com/Syphon/Syphon-Framework/tar.gz/$commit"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dep="$root/dep/Syphon"
work=$(mktemp -d "${TMPDIR:-/tmp}/rvx-syphon.XXXXXX")
trap 'find "$work" -depth -delete' EXIT HUP INT TERM

archive="$work/syphon.tar.gz"
source_dir="$work/source"
object_dir="$work/objects"
mkdir -p "$source_dir" "$object_dir" "$dep/include/Syphon" "$dep/lib"

curl -L --fail --silent --show-error "$archive_url" -o "$archive"
actual_sha256=$(shasum -a 256 "$archive" | awk '{print $1}')
if [ "$actual_sha256" != "$archive_sha256" ]; then
    echo "Syphon archive checksum mismatch: $actual_sha256" >&2
    exit 1
fi
tar -xzf "$archive" -C "$source_dir" --strip-components=1

sdk=$(xcrun --sdk macosx --show-sdk-path)
deployment_target=${MACOSX_DEPLOYMENT_TARGET:-11.0}
for header in "$source_dir"/*.h; do
    cp "$header" "$dep/include/Syphon/"
done

sources='SyphonCFMessageReceiver.m
SyphonCFMessageSender.m
SyphonCGL.c
SyphonClientBase.m
SyphonClientConnectionManager.m
SyphonDispatch.c
SyphonGLShader.m
SyphonGLVertices.m
SyphonIOSurfaceImageCore.m
SyphonIOSurfaceImageLegacy.m
SyphonImageBase.m
SyphonMessageQueue.m
SyphonMessageReceiver.m
SyphonMessageSender.m
SyphonMessaging.m
SyphonOpenGLClient.m
SyphonOpenGLFunctions.c
SyphonOpenGLImage.m
SyphonOpenGLServer.m
SyphonPrivate.m
SyphonServerBase.m
SyphonServerConnectionManager.m
SyphonServerDirectory.m
SyphonServerGLShader.m
SyphonServerGLVertices.m
SyphonServerRendererCoreGL.m
SyphonServerRendererGL.m
SyphonServerRendererLegacyGL.m'

old_ifs=$IFS
IFS='
'
for name in $sources; do
    case "$name" in
        *.m)
            clang -arch arm64 "-mmacosx-version-min=$deployment_target" -isysroot "$sdk" \
                -O2 -DNDEBUG -DSYPHON_CORE_SHARE -DGL_SILENCE_DEPRECATION \
                -I"$source_dir" -I"$dep/include" -fobjc-arc -fblocks \
                -include "$source_dir/Syphon_Prefix.pch" \
                -c "$source_dir/$name" -o "$object_dir/${name%.*}.o"
            ;;
        *.c)
            clang -arch arm64 "-mmacosx-version-min=$deployment_target" -isysroot "$sdk" \
                -O2 -DNDEBUG -DSYPHON_CORE_SHARE -DGL_SILENCE_DEPRECATION \
                -I"$source_dir" -I"$dep/include" -fblocks \
                -c "$source_dir/$name" -o "$object_dir/${name%.*}.o"
            ;;
    esac
done
IFS=$old_ifs

libtool -static -o "$dep/libSyphon.a" "$object_dir"/*.o
cp "$source_dir/License.txt" "$dep/LICENSE.txt"
printf '%s\n' "$commit" > "$dep/COMMIT"
{
    printf 'commit=%s\n' "$commit"
    printf 'archive_sha256=%s\n' "$archive_sha256"
    printf 'architecture=arm64\n'
    printf 'deployment_target=%s\n' "$deployment_target"
    printf 'sdk=%s\n' "$sdk"
    printf 'flags=-O2 -DNDEBUG -fobjc-arc -fblocks -DSYPHON_CORE_SHARE -DGL_SILENCE_DEPRECATION\n'
    clang --version | sed -n '1p'
} > "$dep/BUILD-INFO.txt"

echo "Built $dep/libSyphon.a"
echo "Headers: $dep/include"
echo "Link with: -Wl,-force_load,$dep/libSyphon.a -framework Foundation -framework AppKit -framework OpenGL -framework IOSurface -framework CoreVideo"
