FROM mcr.microsoft.com/dotnet/sdk:10.0@sha256:0d99ebf1ccad15037b5c98c9fe8979d6b8dbac23812feae2277a0b277b5c5ffa AS build
ENV DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 DOTNET_PROCESSOR_COUNT=2
WORKDIR /src
COPY vendor/LibProsperoPkg/ vendor/LibProsperoPkg/
COPY Directory.Build.targets vendor/LibProsperoPkg/Directory.Build.targets
COPY patches/ patches/
RUN cd vendor/LibProsperoPkg && git apply --check /src/patches/0001-store-inner-files-raw.patch && git apply /src/patches/0001-store-inner-files-raw.patch && git apply --check /src/patches/0002-multiple-inode-blocks.patch && git apply /src/patches/0002-multiple-inode-blocks.patch && git apply --check /src/patches/0003-large-layout-indexes.patch && git apply /src/patches/0003-large-layout-indexes.patch && git apply --check /src/patches/0004-naps-padding-count.patch && git apply /src/patches/0004-naps-padding-count.patch && git apply --check /src/patches/0005-outer-double-indirect.patch && git apply /src/patches/0005-outer-double-indirect.patch && git apply --check /src/patches/0006-no-inner-image-clone.patch && git apply /src/patches/0006-no-inner-image-clone.patch && git apply --check /src/patches/0007-bounded-builder-memory.patch && git apply /src/patches/0007-bounded-builder-memory.patch && git apply --check /src/patches/0008-bounded-mount-reconstruction.patch && git apply /src/patches/0008-bounded-mount-reconstruction.patch
COPY Muse.Package/ Muse.Package/
RUN dotnet publish Muse.Package/Muse.Package.csproj -c Release -o /app --disable-build-servers -m:1

FROM mcr.microsoft.com/dotnet/runtime:10.0@sha256:939555726cae4c916d212323ac3c0792996dcac2614f00e82cc40fcf059e1af3
ENV DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 DOTNET_PROCESSOR_COUNT=2
COPY --from=build /app /app
COPY UPSTREAM.json /app/UPSTREAM.json
WORKDIR /work
ENTRYPOINT ["dotnet", "/app/Muse.Package.dll"]
