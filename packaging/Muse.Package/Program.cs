// SPDX-License-Identifier: GPL-3.0-or-later
using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text;
using System.Text.RegularExpressions;
using LibProsperoPkg.PFS;
using System.Text.Json.Serialization;
using LibProsperoPkg;
using LibProsperoPkg.Content;
using LibProsperoPkg.PKG;

internal static class Program
{
    private const string Revision = "be79990357efa6c1a873ae7443dec6e640172914";
    private static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() }
    };
    private static readonly FselfOptions Fself = new() { NormalizeHeader = false };

    private static int Main(string[] args)
    {
        try
        {
            if (LibProsperoPkg.PlayGo.ProsperoPlayGo.GetRightSprx() is not null)
                throw new InvalidOperationException("Build configuration error: opaque upstream right.sprx resource is still embedded.");
            if (args.Length == 0) throw new ArgumentException(
                "Usage: build|build-generated TITLE_FOLDER OUTPUT_FOLDER | preflight|preflight-generated TITLE_FOLDER | inspect PACKAGE.pkg");
            switch (args[0])
            {
                case "version" when args.Length == 1:
                    Console.WriteLine(JsonSerializer.Serialize(new
                    {
                        UpstreamRevision = Revision, OpaqueRightsResourceIncluded = false,
                        InnerStorage = "Existing PS5 raw-block format (packaging patch 0001)",
                        InodeLayout = "Multiple padded inode blocks (packaging patch 0002)",
                        NapsIndexes = "Typed 24-bit bases, block-aligned raw files (packaging patch 0003)",
                        NapsPadding = "Header-aware padding count (packaging patch 0004)",
                        OuterPointers = "Single/double indirection (packaging patch 0005)",
                        ExtractionMemory = "Direct block-backed mount reconstruction with no full inner copy (patches 0006/0008)",
                        AssemblyMemory = "Bounded source writers, shared final mount buffer and probe reuse (patch 0007)",
                        Runtime = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription
                    }, Json)); break;
                case "format-checks" when args.Length == 1: FormatChecks(); break;
                case "outer-checks" when args.Length == 1: OuterChecks(); break;
                case "build" when args.Length == 3: Build(args[1], args[2]); break;
                case "build-generated" when args.Length == 3: Build(args[1], args[2], generated: true); break;
                case "preflight-generated" when args.Length == 2:
                    Console.WriteLine(JsonSerializer.Serialize(Preflight(args[1], generated: true), Json)); break;
                case "preflight" when args.Length == 2:
                    Console.WriteLine(JsonSerializer.Serialize(Preflight(args[1]), Json)); break;
                case "inspect" when args.Length == 2:
                    var report = ProsperoPkgValidator.Validate(args[1]);
                    Console.WriteLine(JsonSerializer.Serialize(new
                    {
                        report.Accepted, report.Checks,
                        HardwareVerified = false,
                        Limitation = "Structural inspection does not verify installation or execution on firmware 12.40."
                    }, Json));
                    return report.Accepted ? 0 : 1;
                default: throw new ArgumentException("Unknown command or incorrect argument count.");
            }
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"PACKAGE ERROR: {e}");
            return 1;
        }
    }

    private sealed record Title(string ContentId, string TitleId, string Name, string Version,
        string EbootType, string[] Models, int FileCount, long InputBytes);

    private static Title Preflight(string source, bool generated = false)
    {
        source = Path.GetFullPath(source);
        if (!Directory.Exists(source)) throw new IOException($"Title folder is missing: {source}");
        // Check every directory before recursing, so symlinks cannot pull unrelated host files into a package.
        var files = SafeFiles(source).ToArray();
        string param = Path.Combine(source, "sce_sys/param.json");
        if (!File.Exists(param)) throw new IOException("sce_sys/param.json is required.");
        if (File.Exists(Path.Combine(source, "sce_sys/param.sfo")))
            throw new IOException("PS4 param.sfo is not valid PS5 title metadata.");
        using var doc = JsonDocument.Parse(File.ReadAllBytes(param));
        var metadata = doc.RootElement;
        string content = metadata.GetProperty("contentId").GetString() ?? "";
        string titleId = metadata.GetProperty("titleId").GetString() ?? "";
        if (!ProsperoPackageBuilder.IsValidContentId(content) || !ProsperoPackageBuilder.IsValidTitleId(titleId)
            || content.Substring(7, 9) != titleId)
            throw new InvalidDataException("param.json titleId and contentId must be valid and consistent.");
        if (generated && (!content.StartsWith("IV0000-", StringComparison.Ordinal) ||
            !titleId.StartsWith("PPSA99", StringComparison.Ordinal) || titleId == "PPSA99576"))
            throw new InvalidDataException("Generated homebrew requires IV0000/PPSA99xxx identity and cannot replace Muse itself.");
        string version = metadata.TryGetProperty("contentVersion", out var cv) ? cv.GetString() ?? "01.000.000" : "01.000.000";
        string name = titleId;
        if (metadata.TryGetProperty("localizedParameters", out var localized))
        {
            foreach (var locale in localized.EnumerateObject())
                if (locale.Value.ValueKind == JsonValueKind.Object && locale.Value.TryGetProperty("titleName", out var tn))
                { name = tn.GetString() ?? titleId; break; }
        }
        string eboot = ValidateNativeModule(Path.Combine(source, "eboot.bin"), true);
        foreach (string file in files.Where(IsModule).Where(f => Path.GetFileName(f) != "eboot.bin"))
            ValidateNativeModule(file, false);
        foreach (string art in new[] { "icon0.png", "pic1.png" })
        {
            string file = Path.Combine(source, "sce_sys", art);
            if (!File.Exists(file)) throw new IOException($"sce_sys/{art} is required.");
            // Exercise the actual PNG decoder and BC7 encoder; a renamed/invalid PNG must not pass silently.
            byte[] dds = ProsperoDdsEncoder.EncodePngToDds(File.ReadAllBytes(file));
            if (dds.Length < 148) throw new InvalidDataException($"Invalid image: {art}");
        }
        var models = files.Where(f => f.EndsWith(".gguf", StringComparison.OrdinalIgnoreCase)).ToArray();
        if (models.Length == 0 && !generated) throw new IOException("The all-in-one title must include at least one GGUF model.");
        var modelHeader = new byte[8];
        foreach (string model in models)
        {
            using var stream = File.OpenRead(model);
            Span<byte> head = modelHeader;
            stream.ReadExactly(head);
            if (!head[..4].SequenceEqual("GGUF"u8) || BinaryPrimitives.ReadUInt32LittleEndian(head[4..]) is < 2 or > 3)
                throw new InvalidDataException($"Invalid GGUF model header: {model}");
        }
        var readiness = ProsperoLaunchReadiness.InspectAppRoot(source);
        if (!readiness.IsLaunchReady) throw new InvalidDataException(string.Join("; ", readiness.Issues));
        return new Title(content, titleId, name, version, eboot,
            models.Select(f => Path.GetRelativePath(source, f)).ToArray(), files.Length,
            files.Sum(f => new FileInfo(f).Length));
    }

    private static IEnumerable<string> SafeFiles(string root)
    {
        if ((File.GetAttributes(root) & FileAttributes.ReparsePoint) != 0)
            throw new IOException($"Symlinks are not accepted in title inputs: {root}");
        foreach (string entry in Directory.EnumerateFileSystemEntries(root).Order(StringComparer.Ordinal))
        {
            var attributes = File.GetAttributes(entry);
            if ((attributes & FileAttributes.ReparsePoint) != 0)
                throw new IOException($"Symlinks are not accepted in title inputs: {entry}");
            if ((attributes & FileAttributes.Directory) != 0)
            {
                if (Path.GetFileName(entry) == ".git") throw new IOException("A title must not contain .git metadata.");
                foreach (string nested in SafeFiles(entry)) yield return nested;
            }
            else yield return entry;
        }
    }

    private static bool IsModule(string path) => Path.GetFileName(path) == "eboot.bin" ||
        new[] { ".elf", ".prx", ".sprx" }.Contains(Path.GetExtension(path).ToLowerInvariant());

    private static string ValidateNativeModule(string file, bool main)
    {
        if (!File.Exists(file)) throw new IOException($"Missing native executable: {file}");
        using var stream = File.OpenRead(file);
        var bytes = new byte[Math.Min(stream.Length, 2 * 1024 * 1024)];
        stream.ReadExactly(bytes);
        ReadOnlySpan<byte> elf = bytes;
        bool modernSelf = bytes.Length >= 32 && BinaryPrimitives.ReadUInt32LittleEndian(bytes) == 0xeef51454;
        bool self = ProsperoFself.IsSelf(bytes) || modernSelf;
        if (self)
        {
            // The native kit emits both documented container magics. LibProsperoPkg
            // only recognizes the alternate magic; normalize our private header view
            // for its parser, preserving the source and package bytes exactly.
            if (modernSelf) BinaryPrimitives.WriteUInt32LittleEndian(bytes, ProsperoFself.Magic);
            var inspected = ProsperoLaunchReadiness.InspectModule(file, bytes);
            if (!inspected.WillRunOnDebugConsole) throw new InvalidDataException(inspected.Note);
            elf = ProsperoFself.Parse(bytes).Elf;
        }
        if (!ProsperoFself.IsElf(elf) || elf[4] != 2 || elf[5] != 1 || elf[7] != 9 || elf[8] != 2 ||
            BinaryPrimitives.ReadUInt16LittleEndian(elf[18..]) != 62)
            throw new InvalidDataException($"{file} is not a native x86-64 FreeBSD ELF/SELF.");
        ushort type = BinaryPrimitives.ReadUInt16LittleEndian(elf[16..]);
        if (main ? type != 0xfe10 : type is not (0xfe10 or 0xfe18))
            throw new InvalidDataException($"{file}: ELF type 0x{type:X4}; expected native {(main ? "0xFE10" : "0xFE10/0xFE18")}. An elfldr payload cannot become a native title by renaming it.");
        ushort phsize = BinaryPrimitives.ReadUInt16LittleEndian(elf[54..]);
        ushort phcount = BinaryPrimitives.ReadUInt16LittleEndian(elf[56..]);
        ulong phoff = BinaryPrimitives.ReadUInt64LittleEndian(elf[32..]);
        if (phsize != 56 || phcount == 0 || phoff > (ulong)elf.Length ||
            phoff + (ulong)phcount * phsize > (ulong)elf.Length)
            throw new InvalidDataException($"{file}: malformed program-header table.");
        bool dynamic = false, linkingLoad = false, procParam = false;
        for (int n = 0; n < phcount; n++)
        {
            ReadOnlySpan<byte> ph = elf[(int)(phoff + (ulong)n * phsize)..];
            uint segmentType = BinaryPrimitives.ReadUInt32LittleEndian(ph);
            uint flags = BinaryPrimitives.ReadUInt32LittleEndian(ph[4..]);
            dynamic |= segmentType == 2;
            linkingLoad |= segmentType == 1 && flags == 0;
            procParam |= segmentType == 0x61000001;
            if (segmentType == 1 && flags != 0)
            {
                ulong offset = BinaryPrimitives.ReadUInt64LittleEndian(ph[8..]);
                ulong address = BinaryPrimitives.ReadUInt64LittleEndian(ph[16..]);
                ulong fileSize = BinaryPrimitives.ReadUInt64LittleEndian(ph[32..]);
                ulong memorySize = BinaryPrimitives.ReadUInt64LittleEndian(ph[40..]);
                ulong alignment = BinaryPrimitives.ReadUInt64LittleEndian(ph[48..]);
                if (alignment != 0x4000 || offset % 0x4000 != address % 0x4000 || memorySize < fileSize)
                    throw new InvalidDataException($"{file}: invalid native 16 KiB LOAD alignment/size.");
            }
        }
        // Match the current public native converter and its inspect.ps1. PS5 uses
        // a flags-zero linking LOAD, not the older PT_SCE_DYNLIBDATA convention.
        if (!dynamic || !linkingLoad || (main && !procParam))
            throw new InvalidDataException($"{file}: missing native dynamic/linking/process program headers.");
        return $"{(self ? "SELF" : "ELF")} / native 0x{type:X4}";
    }

    private static void Build(string source, string output, bool generated = false)
    {
        source = Path.GetFullPath(source);
        output = Path.GetFullPath(output);
        if (output == source || output.StartsWith(source + Path.DirectorySeparatorChar, StringComparison.Ordinal))
            throw new ArgumentException("Output must be outside the source title folder.");
        Title title = Preflight(source, generated);
        Directory.CreateDirectory(output);
        string work = Path.Combine(output, ".muse-package-" + Guid.NewGuid().ToString("N"));
        string stage = Path.Combine(work, "stage"), extracted = Path.Combine(work, "extracted");
        Directory.CreateDirectory(stage);
        try
        {
            // HomebrewPackager only copies modules and sce_sys. The full builder preserves models/assets.
            foreach (string file in SafeFiles(source))
            {
                string dest = Path.Combine(stage, Path.GetRelativePath(source, file));
                Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
                File.Copy(file, dest);
            }
            var result = ProsperoPackageBuilder.Build(new ProsperoBuildOptions
            {
                Mode = ProsperoPackageMode.Homebrew,
                OutputFormat = ProsperoOutputFormat.DebugImage,
                SourceFolder = stage, OutputFolder = work,
                ContentId = title.ContentId, TitleId = title.TitleId, Title = title.Name,
                Version = "01.00", LicenseFree = true,
                GenerateParamJsonIfMissing = false, FselfOptions = Fself
            }, Console.WriteLine);
            var validation = ProsperoPkgValidator.Validate(result.OutputPath, title.ContentId);
            if (!validation.Accepted) throw new InvalidDataException("Package structural validation failed.");
            var pkg = ProsperoPkgReader.Read(result.OutputPath);
            var manifest = ProsperoPackageExtractor.Extract(result.OutputPath, extracted,
                ProsperoExtractionKey.FromPasscode(new string('0', 32)),
                new ProsperoExtractionOptions { ExtractOuterMetadata = true }, Console.WriteLine);
            var napsCheck = ValidateNaps(File.ReadAllBytes(Path.Combine(extracted, "_outer/naps_pkg_layout.dat")));
            // Metadata is stored in the CNT, separate from the extracted application filesystem.
            ExtractSystemEntries(result.OutputPath, pkg, extracted);
            var hashes = new List<object>();
            foreach (string file in SafeFiles(source))
            {
                string relative = Path.GetRelativePath(source, file);
                string actual = Path.Combine(extracted, relative);
                if (!File.Exists(actual)) throw new InvalidDataException($"Package lost source file: {relative}");
                string sourceHash = Hash(file);
                string expected = sourceHash;
                string transformation = "none";
                if (relative.Replace('\\', '/') == "sce_sys/param.json")
                {
                    string text = File.ReadAllText(file);
                    foreach (string key in new[] { "sdkVersion", "requiredSystemSoftwareVersion" })
                        text = Regex.Replace(text, "(\"" + key + "\"\\s*:\\s*\")0x0+(\")",
                            "${1}0x0200000000000000$2", RegexOptions.CultureInvariant);
                    expected = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
                    if (expected != sourceHash) transformation = "upstream zero SDK/firmware version promotion to 2.00";
                }
                if (IsModule(file))
                {
                    byte[] bytes = File.ReadAllBytes(file);
                    if (ProsperoFself.IsElf(bytes))
                    {
                        transformation = "native ELF to development SELF";
                        expected = Convert.ToHexStringLower(SHA256.HashData(ProsperoFself.MakeFself(bytes, Fself)));
                    }
                }
                string got = Hash(actual);
                if (got != expected) throw new InvalidDataException($"Round-trip hash mismatch: {relative}");
                hashes.Add(new { path = relative, bytes = new FileInfo(actual).Length, sha256 = got, sourceSha256 = sourceHash, transformation });
            }
            var readiness = ProsperoLaunchReadiness.InspectAppRoot(extracted);
            if (!readiness.IsLaunchReady) throw new InvalidDataException(string.Join("; ", readiness.Issues));
            var nativeValidation = SafeFiles(extracted).Where(IsModule).Select(module => new
            {
                Path = Path.GetRelativePath(extracted, module),
                Type = ValidateNativeModule(module, Path.GetFileName(module) == "eboot.bin")
            }).ToArray();
            if (generated && new FileInfo(result.OutputPath).Length > 1024L * 1024 * 1024)
                throw new InvalidDataException("Generated package exceeds the gadget installer limit of 1 GiB.");
            string final = Path.Combine(output, title.ContentId + ".pkg");
            File.Move(result.OutputPath, final, true);
            var report = new
            {
                SchemaVersion = 1, BuiltAtUtc = DateTimeOffset.UtcNow, UpstreamRevision = Revision,
                Title = title, GeneratedApplication = generated, Package = Path.GetFileName(final), Bytes = new FileInfo(final).Length,
                Sha256 = Hash(final), StructuralValidation = validation,
                ModuleReadiness = readiness, NativeModuleValidation = nativeValidation, NapsIndexValidation = napsCheck,
                RoundTripFileCount = hashes.Count, Files = hashes,
                Warnings = result.Warnings, HardwareVerified = false,
                InnerStorage = "Existing PS5 raw-block format (packaging patch 0001)",
                        InodeLayout = "Multiple padded inode blocks (packaging patch 0002)",
                        NapsIndexes = "Typed 24-bit bases, block-aligned raw files (packaging patch 0003)",
                        NapsPadding = "Header-aware padding count (packaging patch 0004)",
                        OuterPointers = "Single/double indirection (packaging patch 0005)",
                        ExtractionMemory = "Direct block-backed mount reconstruction with no full inner copy (patches 0006/0008)",
                OmittedUpstreamResource = "PlayGo/Data/right.sprx: optional prebuilt module without corresponding source/provenance in upstream NOTICE",
                Limitation = "Structural/package verification only. Installation, launch, graphics, audio, inference and firmware 12.40 compatibility require the owner's console test."
            };
            File.WriteAllText(final + ".report.json", JsonSerializer.Serialize(report, Json));
            File.WriteAllText(final + ".sha256", $"{Hash(final)}  {Path.GetFileName(final)}\n");
            Console.WriteLine($"VERIFIED PACKAGE: {final}");
            Console.WriteLine($"Round-trip checked {hashes.Count} input files; hardware verified: false.");
        }
        finally { if (Directory.Exists(work)) Directory.Delete(work, true); }
    }

    private static object ValidateNaps(byte[] bytes)
    {
        var doc = ProsperoNapsLayout.Parse(bytes);
        if (!bytes.AsSpan().SequenceEqual(ProsperoNapsLayout.BuildLayout(doc)))
            throw new InvalidDataException("NAPS serialized table does not round-trip.");
        foreach (var group in doc.CblockInfoOffsetByUblock)
            foreach (uint index in group.StartCblockInfoIndex)
                if (index >= doc.CblockInfos.Count || doc.CblockInfos[(int)index].IsRunBase)
                    throw new InvalidDataException("NAPS lookup index does not address a block descriptor.");
        return new { SerializedRoundTrip = true, Groups = doc.CblockInfoOffsetByUblock.Count,
            MaximumBase = doc.CblockInfoOffsetByUblock.Max(x => x.InfoOffset9BBase) };
    }

    private static void FormatChecks()
    {
        SourceWriterChecks();
        foreach (int count in new[] { 390, 391, 780, 781 })
        {
            var nodes = Enumerable.Range(0, count).Select(i => new ProsperoPs5MetaNode
                { Inode = (uint)i, Mode = 0x816d, Size = i + 1 }).ToArray();
            byte[] meta = new ProsperoPs5InnerMetadata(0, 0).Build(nodes, 2048, new[] { new byte[64] });
            int blocks = (count + 389) / 390;
            foreach (int offset in new[] { 0x40, 0xb0 })
                if (BinaryPrimitives.ReadInt64LittleEndian(meta.AsSpan(offset)) != blocks)
                    throw new InvalidDataException("Inode descriptor block count mismatch.");
            foreach (int offset in new[] { 0x58, 0x60 })
                if (BinaryPrimitives.ReadInt64LittleEndian(meta.AsSpan(offset)) != (long)blocks * 65536)
                    throw new InvalidDataException("Inode descriptor byte count mismatch.");
            for (int i = 0; i < count; i++)
            {
                int offset = 65536 + (i / 390) * 65536 + (i % 390) * 168;
                if (BinaryPrimitives.ReadInt64LittleEndian(meta.AsSpan(offset + 8)) != i + 1)
                    throw new InvalidDataException("Inode block padding/index mismatch.");
            }
        }
        foreach (int count in Enumerable.Range(510, 16))
        {
            var blocks = Enumerable.Range(0, count).Select(i => new NapsCblockPlanEntry
            {
                StartRun = true, OnDiskOffset = (long)i * 262144, LogicalOffset = (long)i * 262144,
                EvenChunkCompressedLength = 65536, StreamLength = 262144,
                Even = 1, Odd = 1, KdePredictor = 4
            }).ToList();
            blocks.Add(new NapsCblockPlanEntry { StartRun = true, LogicalOffset = (long)count * 262144, Terminator = true });
            var doc = ProsperoNapsLayoutBuilder.BuildDocument(new NapsGenerationRequest
            {
                NumUBlocks = count, NumOuterBlocks = count * 4, Blocks = blocks,
                FileLogicalOffsets = new long[] { 0, (long)count * 262144 }
            });
            byte[] bytes = ProsperoNapsLayout.BuildLayout(doc);
            ValidateNaps(bytes);
            var decoded = ProsperoNapsLayout.Parse(bytes);
            for (int g = 0; g < decoded.CblockInfoOffsetByUblock.Count; g++)
                for (int j = 0; j < 8; j++)
                {
                    uint expected = (uint)(Math.Min(g * 8 + j, count) * 2 + 1);
                    if (decoded.CblockInfoOffsetByUblock[g].StartCblockInfoIndex[j] != expected)
                        throw new InvalidDataException("24-bit NAPS base/delta/trailing-group mismatch.");
                }
        }
        Console.WriteLine("PASS: inode boundaries 390/391/780/781; serialized NAPS bases above 255, trailing groups and padding alignments");
    }

    private static void SourceWriterChecks()
    {
        // Compare complete deterministic inner images, including executable flags,
        // metadata and padding, for memory input versus the actual file-writer path.
        string directory = Path.Combine(Path.GetTempPath(), "muse-source-writers-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var root = new ProsperoFsDir();
            var files = new List<ProsperoPs5InnerFile>();
            foreach (int length in new[] { 0, 1, 3, 4, 65535, 65536, 65537, 262151 })
            {
                var bytes = new byte[length];
                for (int i = 0; i < length; i++) bytes[i] = (byte)(i * 37 + 11);
                if (length >= 4) "\u007fELF"u8.CopyTo(bytes);
                string name = "input-" + length;
                string path = Path.Combine(directory, name);
                File.WriteAllBytes(path, bytes);
                root.Files.Add(new ProsperoFsFile(path) { name = name, Parent = root });
                files.Add(new ProsperoPs5InnerFile { Path = "/" + name, Data = bytes });
            }
            var assembler = new ProsperoPs5InnerImageAssembler(12345, 0);
            var memory = assembler.Build(files);
            var streamed = assembler.BuildFromFsTree(root);
            if (!memory.Image.AsSpan().SequenceEqual(streamed.Image) ||
                !memory.MetadataPlaintext.AsSpan().SequenceEqual(streamed.MetadataPlaintext))
                throw new InvalidDataException("Streamed and in-memory inner images differ.");
            byte[] naps = ProsperoNwonlyNapsGenerator.Generate(memory);
            var spanMount = ProsperoPs5InnerImageReader.ReconstructMount(memory.Image, naps);
            using var blockReader = new TrackingReader(memory.Image);
            var readerMount = ProsperoPs5InnerImageReader.ReconstructMount(blockReader, memory.Image.Length, naps);
            if (!spanMount.Mount.AsSpan().SequenceEqual(readerMount.Mount) ||
                spanMount.SuperblockOffset != readerMount.SuperblockOffset ||
                blockReader.MaximumRead > 262144 || blockReader.ReadCount < 2)
                throw new InvalidDataException("Block-backed reconstruction differs or performed an oversized read.");
            bool capacityRejected = false;
            try { ProsperoPs5InnerImageReader.ReconstructMount(blockReader, memory.Image.Length, naps, (long)Array.MaxLength + 1); }
            catch (ArgumentOutOfRangeException) { capacityRejected = true; }
            if (!capacityRejected) throw new InvalidDataException("Oversized mount was not rejected before allocation.");
            bool truncatedRejected = false;
            using var truncated = new TrackingReader(memory.Image[..4]);
            try { ProsperoPs5InnerImageReader.ReconstructMount(truncated, memory.Image.Length, naps); }
            catch (EndOfStreamException) { truncatedRejected = true; }
            if (!truncatedRejected) throw new InvalidDataException("Short source reader was silently accepted.");
            foreach (int actual in new[] { 3, 5 })
            {
                bool rejected = false;
                try
                {
                    new ProsperoPs5InnerImageBuilder().Build(new[] {
                        new ProsperoPs5InnerPayload { StoreRaw = true, SourceLength = 4,
                            SourceWriter = stream => stream.Write(new byte[actual]) } });
                }
                catch (Exception e) when (e is InvalidDataException or NotSupportedException)
                { rejected = true; }
                if (!rejected) throw new InvalidDataException("Mismatched writer length was accepted.");
            }
            File.AppendAllText(Path.Combine(directory, "input-4"), "changed");
            bool changed = false;
            try { assembler.BuildFromFsTree(root); }
            catch (InvalidDataException) { changed = true; }
            if (!changed) throw new InvalidDataException("Changed source length was accepted.");
        }
        finally { Directory.Delete(directory, recursive: true); }
        Console.WriteLine("PASS: streamed/file input equals full in-memory image; empty, magic, block edges, short/long and changed sources");
        Console.WriteLine($"PASS: block-backed reconstruction matches span byte-for-byte with reads <=256KiB; truncated reader and mounts above Array.MaxLength={Array.MaxLength} rejected");
    }

    private sealed class TrackingReader(byte[] bytes) : LibProsperoPkg.Util.IMemoryReader
    {
        public int MaximumRead { get; private set; }
        public int ReadCount { get; private set; }
        public void Read(long pos, byte[] destination, int offset, int count)
        {
            if (pos < 0 || count < 0 || pos > bytes.Length - (long)count)
                throw new EndOfStreamException("Fixture source is shorter than its declared extent.");
            MaximumRead = Math.Max(MaximumRead, count); ReadCount++;
            bytes.AsSpan((int)pos, count).CopyTo(destination.AsSpan(offset, count));
        }
        public void Dispose() { }
    }

    private static void OuterChecks()
    {
        foreach (int blocks in new[] { 12, 13, 1832, 1833, 9113 })
        {
            CheckOuterFile(blocks);
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
        Console.WriteLine("PASS: outer PFS direct/single/double boundaries and former 9112-block limit; unmodified generic reader and pointer hashes");
    }

    private static void CheckOuterFile(int count)
    {
        const int blockSize = ProsperoOuterPfsBuilder.BlockSize;
        var data = new byte[checked(count * blockSize - 7)];
        for (int i = 0; i < count; i++)
            BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(i * blockSize), i + 10001);
        var build = ProsperoOuterPfsBuilder.BuildPlaintext(
            new[] { new ProsperoOuterFile { Name = "pfs_image.dat", Data = data } },
            new ProsperoOuterPfsBuildParameters());
        using var stream = new MemoryStream(build.Plaintext, writable: false);
        using var memory = new LibProsperoPkg.Util.StreamReader(stream);
        var reader = new ProsperoPfsReader(memory,
            superblockByteOffset: (long)build.SuperblockIndex * blockSize, skipDecryption: true);
        var file = reader.GetFile("pfs_image.dat") ?? throw new InvalidDataException("Outer fixture missing.");
        using var actual = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var buffer = new byte[blockSize];
        var view = file.GetView();
        for (long pos = 0; pos < data.Length; pos += buffer.Length)
        {
            int n = (int)Math.Min(buffer.Length, data.Length - pos);
            view.Read(pos, buffer, 0, n);
            actual.AppendData(buffer, 0, n);
        }
        if (file.size != data.Length || !actual.GetHashAndReset().AsSpan().SequenceEqual(SHA256.HashData(data)))
            throw new InvalidDataException("Outer fixture readback mismatch.");
        stream.Position = (long)build.InodeTableIndex * blockSize + 3 * ProsperoDinodeS32.SizeOf;
        var inode = ProsperoDinodeS32.ReadFromStream(stream);
        void HashPointer(int block, byte[] signature)
        {
            if (!signature.AsSpan().SequenceEqual(ProsperoOuterPfsSignature.ComputeBlockHash(
                build.Plaintext.AsSpan(checked(block * blockSize), blockSize))))
                throw new InvalidDataException("Outer pointer signature mismatch.");
        }
        if (count > 12) HashPointer(inode.ib[0].block, inode.ib[0].sig);
        if (count > 1832)
        {
            HashPointer(inode.ib[1].block, inode.ib[1].sig);
            int leaves = (count - 1832 + 1819) / 1820;
            for (int i = 0; i < leaves; i++)
            {
                int offset = inode.ib[1].block * blockSize + i * 36;
                int block = BinaryPrimitives.ReadInt32LittleEndian(build.Plaintext.AsSpan(offset + 32));
                HashPointer(block, build.Plaintext.AsSpan(offset, 32).ToArray());
            }
        }
        Console.WriteLine($"PASS: outer file {count} blocks / {data.Length} bytes, full SHA-256 readback and indirect signatures");
    }

    private static void ExtractSystemEntries(string path, ProsperoPkg pkg, string destination)
    {
        using var stream = File.OpenRead(path);
        foreach (var entry in pkg.Entries)
        {
            string? name = entry.RawId == 0x2000 ? "param.json" :
                ProsperoCntEntryNames.IdToName.GetValueOrDefault((ProsperoCntEntryId)entry.RawId);
            if (name == null || entry.Encrypted) continue;
            string root = Path.GetFullPath(Path.Combine(destination, "sce_sys"));
            string target = Path.GetFullPath(Path.Combine(root, name));
            if (!target.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.Ordinal))
                throw new InvalidDataException("Unsafe system entry path.");
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            stream.Position = checked((long)(pkg.Fih?.EmbeddedCntOffset ?? 0) + entry.DataOffset);
            byte[] data = new byte[entry.DataSize];
            stream.ReadExactly(data);
            File.WriteAllBytes(target, data);
        }
    }

    private static string Hash(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexStringLower(SHA256.HashData(stream));
    }
}
