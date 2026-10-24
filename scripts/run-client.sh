#!/usr/bin/env bash
# Launch the dev client with the HOST driver stack (Mesa/RADV) and a Java 25 runtime, in an ISOLATED game dir
# (fabric/run). Never touches ~/.minecraft. Build this worktree with Gradle first.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JAVA="${JAVA25:-$HOME/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/java/java-runtime-epsilon/bin/java}"
CP="$(cat "${FSR4_CLASSPATH_FILE:-$ROOT/fabric/build/run-classpath.txt}")"
RUN="${FSR4_GAME_DIR:-$ROOT/fabric/run}"; mkdir -p "$RUN"
CFG="${FSR4_LAUNCH_CONFIG:-$ROOT/fabric/.gradle/loom-cache/launch.cfg}"
ASSETS="${ASSETS_DIR:-$HOME/.gradle/caches/fabric-loom/assets}"
exec "$JAVA" --enable-native-access=ALL-UNNAMED --add-exports java.base/jdk.internal.misc=ALL-UNNAMED \
  -Xmx3G ${JVM_EXTRA:-} \
  -Dfabric.dli.config="$CFG" -Dfabric.dli.env=client -Dfabric.dli.main=net.fabricmc.loader.impl.launch.knot.KnotClient \
  -cp "$CP" net.fabricmc.devlaunchinjector.Main \
  --username Dev --version 26.3 --gameDir "$RUN" --assetsDir "$ASSETS" --assetIndex 26.3-34 \
  --uuid 00000000-0000-0000-0000-000000000000 --accessToken 0 --versionType release "$@"
