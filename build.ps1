[CmdletBinding()]
param(
    [switch]$Test,
    [string]$TestFilter = '',
    [switch]$Package,
    [switch]$PackageOnly,
    [string]$Version = 'v1.0.25',
    [string]$CompilerBin = '',
    [string]$OutputDirectory = 'dist/sc2_coop',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Set-Location $PSScriptRoot
$projectRoot = $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build'
$outputDir = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }
New-Item -ItemType Directory -Force $buildDir | Out-Null

function Invoke-Tool([string]$Executable, [string[]]$ToolArgs) {
    $ErrorActionPreference = 'Continue'
    $messages = & $Executable @ToolArgs 2>&1
    $code = $LASTEXITCODE
    foreach ($message in $messages) { Write-Host $message.ToString() }
    if ($code -ne 0) { throw "$Executable exited with code $code" }
}
function Find-Compiler {
    if ($CompilerBin) { return $CompilerBin }
    $found = Get-Command 'clang++.exe' -ErrorAction SilentlyContinue
    if ($found -and (Test-Path (Join-Path (Split-Path $found.Source) 'x86_64-w64-mingw32-windres.exe'))) {
        return Split-Path $found.Source
    }
    $packages = Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages'
    $found = @(Get-ChildItem $packages -Directory -Filter 'MartinStorsjo.LLVM-MinGW*' -ErrorAction SilentlyContinue |
        ForEach-Object { Get-ChildItem $_.FullName -Filter 'clang++.exe' -Recurse -File })
    if ($found.Count) { return $found[0].DirectoryName }
    throw 'LLVM-MinGW not found. Install: winget install MartinStorsjo.LLVM-MinGW.UCRT; or use -CompilerBin.'
}
function Header-Time([string[]]$Directories) {
    $latest = [datetime]::MinValue
    foreach ($dir in $Directories) {
        Get-ChildItem (Join-Path $projectRoot $dir) -Recurse -File | Where-Object { $_.Extension -in '.h','.hpp' } |
            ForEach-Object { if ($_.LastWriteTimeUtc -gt $latest) { $latest = $_.LastWriteTimeUtc } }
    }
    return $latest
}
function Compile([string]$Source, [string]$Object, [datetime]$Headers, [string[]]$Flags, [string]$Compiler) {
    if ((Test-Path $Object) -and (Get-Item $Object).LastWriteTimeUtc -ge (Get-Item $Source).LastWriteTimeUtc -and
        (Get-Item $Object).LastWriteTimeUtc -ge $Headers) { return }
    Write-Host "Compiling $Source"
    Invoke-Tool $Compiler ($Flags + @('-c', $Source, '-o', $Object))
}
function Link([string]$Target, [string[]]$Objects, [string[]]$Options) {
    $needsLink = !(Test-Path $Target)
    if (!$needsLink) {
        $stamp = (Get-Item $Target).LastWriteTimeUtc
        foreach ($obj in $Objects) { if ((Get-Item $obj).LastWriteTimeUtc -gt $stamp) { $needsLink = $true; break } }
    }
    if (!$needsLink) { return }
    # UTF-8 response files preserve Unicode and spaces without shell expansion.
    $rsp = Join-Path $buildDir (([IO.Path]::GetFileNameWithoutExtension($Target)) + '_link.rsp')
    $arguments = @('-static') + $Options + $Objects + $libraries + @('-o', $Target)
    $quoted = $arguments | ForEach-Object { '"' + $_.Replace('\','/').Replace('"','\"') + '"' }
    [IO.File]::WriteAllLines($rsp, [string[]]$quoted, (New-Object Text.UTF8Encoding $false))
    Write-Host "Linking $Target"
    Invoke-Tool $cxx @("@$rsp")
}
function Package-Release {
    $exe = Join-Path $outputDir 'sc2_coop.exe'
    $assets = Join-Path $outputDir 'assets'
    if (!(Test-Path $exe) -or !(Test-Path $assets)) { throw "Missing release build in $outputDir" }
    Add-Type -AssemblyName @('System.IO.Compression','System.IO.Compression.FileSystem')
    $destination = Join-Path $projectRoot "dist/sc2-coop-native-$Version-windows-x64.zip"
    $tempZip = "$destination.tmp"
    if (Test-Path $tempZip) { Remove-Item $tempZip }
    $zip = [IO.Compression.ZipFile]::Open($tempZip, [IO.Compression.ZipArchiveMode]::Create)
    try {
        # Allowlist only the binary and asset types. Runtime databases,
        # personal settings, logs, dumps and previous executables cannot enter.
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $exe, 'sc2_coop/sc2_coop.exe', 'Optimal') | Out-Null
        foreach ($file in Get-ChildItem $assets -Recurse -File | Sort-Object FullName) {
            $languageAsset = $file.DirectoryName -eq (Join-Path $assets 'lang') -and $file.Name -in 'en_US.json','zh_CN.json'
            $licenseAsset = $file.DirectoryName -eq (Join-Path $assets 'licenses') -and $file.Name -in 's2protocol.txt','SC2_Coop_Overlay-notice.txt','SC2_Coop_Overlay-GPL.txt'
            if (!$languageAsset -and !$licenseAsset -and $file.Extension.ToLowerInvariant() -notin '.png','.jpg','.jpeg','.otf','.ttf','.ico') { continue }
            $relative = $file.FullName.Substring($outputDir.Length + 1).Replace('\','/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, "sc2_coop/$relative", 'Optimal') | Out-Null
        }
    } finally { $zip.Dispose() }
    Move-Item -Force $tempZip $destination
    $hash = (Get-FileHash -Algorithm SHA256 $destination).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$destination.sha256", "$hash  $([IO.Path]::GetFileName($destination))`n", (New-Object Text.UTF8Encoding $false))
    Write-Host "Package: $destination"
    Write-Host "SHA256: $hash"
}
if ($PackageOnly) { Package-Release; exit 0 }
$bin = Find-Compiler
$cxx = Join-Path $bin 'clang++.exe'
$cc = Join-Path $bin 'clang.exe'
$ar = Join-Path $bin 'llvm-ar.exe'
$windres = Join-Path $bin 'x86_64-w64-mingw32-windres.exe'
$cxxFlags = @('-std=c++20','-O2','-DUNICODE','-D_UNICODE','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX',
    '-Isrc','-Ivendor','-Ivendor/imgui','-Ivendor/implot','-Ivendor/nlohmann','-Ivendor/stb','-Ivendor/sqlite3','-Ivendor/stormlib')
$libraries = @('-ld3d11','-lbcrypt','-ld3dcompiler','-ldwmapi','-lole32','-lwindowscodecs','-lwininet','-limm32','-lgdi32','-lshell32','-luuid')
$srcHeaders = Header-Time @('src','vendor')
$vendorHeaders = Header-Time @('vendor')
$testHeaders = Header-Time @('src','vendor','tests')
$sources = @(
    'src/main.cpp',
    'src/app_state.cpp',
    'src/i18n/i18n.cpp',
    'src/db/database_schema.cpp',
    'src/db/replay_repository.cpp',
    'src/db/player_repository.cpp',
    'src/db/stats_repository.cpp',
    'src/db/weekly_repository.cpp',
    'src/db/export_repository.cpp',
    'src/replay/mpq_reader.cpp',
    'src/replay/s2_decoder.cpp',
    'src/replay/details_decoder.cpp',
    'src/replay/tracker_decoder.cpp',
    'src/replay/chat_decoder.cpp',
    'src/replay/replay_parser.cpp',
    'src/replay/replay_duration.cpp',
    'src/replay/replay_file_state.cpp',
    'src/replay/replay_discovery.cpp',
    'src/replay/replay_scanner.cpp',
    'src/replay/replay_watcher.cpp',
    'src/utils/win32_utils.cpp',
    'src/utils/hotkey_manager.cpp',
    'src/ui/texture_loader.cpp',
    'src/ui/ui_manager.cpp',
    'src/ui/frame_renderer.cpp',
    'src/ui/hud/summary_overlay.cpp',
    'src/ui/hud/summary_overlay_chart.cpp',
    'src/ui/hud/summary_overlay_render.cpp',
    'src/ui/hud/ally_toast.cpp',
    'src/ui/tabs/tab_settings.cpp',
    'src/ui/tabs/tab_games.cpp',
    'src/ui/tabs/tab_players.cpp',
    'src/ui/tabs/tab_weeklies.cpp',
    'src/ui/tabs/tab_stats.cpp',
    'src/ui/modals/modal_game_detail.cpp',
    'src/ui/modals/modal_chat_view.cpp',
    'src/ui/modals/modal_player_note.cpp',
    'vendor/imgui/imgui.cpp',
    'vendor/imgui/imgui_draw.cpp',
    'vendor/imgui/imgui_tables.cpp',
    'vendor/imgui/imgui_widgets.cpp',
    'vendor/imgui/backends/imgui_impl_win32.cpp',
    'vendor/imgui/backends/imgui_impl_dx11.cpp',
    'vendor/implot/implot.cpp',
    'vendor/implot/implot_items.cpp'
)
$objects = @()
foreach ($source in $sources) {
    if ($Test -and $source -eq 'src/main.cpp') { continue }
    $obj = Join-Path $buildDir ($source.Replace('/','_') + '.o')
    $headerTime = if ($source.StartsWith('vendor/')) { $vendorHeaders } else { $srcHeaders }
    Compile $source $obj $headerTime $cxxFlags $cxx
    $objects += $obj
}
$sqlite = Join-Path $buildDir 'sqlite3.o'
Compile 'vendor/sqlite3/sqlite3.c' $sqlite $vendorHeaders @('-O2') $cc
$objects += $sqlite
$stormCpp = @(
    'FileStream.cpp',
    'SBaseCommon.cpp',
    'SBaseDumpData.cpp',
    'SBaseFileTable.cpp',
    'SBaseSubTypes.cpp',
    'SCompression.cpp',
    'SFileAddFile.cpp',
    'SFileAttributes.cpp',
    'SFileCompactArchive.cpp',
    'SFileCreateArchive.cpp',
    'SFileExtractFile.cpp',
    'SFileFindFile.cpp',
    'SFileGetFileInfo.cpp',
    'SFileListFile.cpp',
    'SFileOpenArchive.cpp',
    'SFileOpenFileEx.cpp',
    'SFilePatchArchives.cpp',
    'SFileReadFile.cpp',
    'SFileVerify.cpp',
    'SMemUtf8.cpp',
    'adpcm/adpcm.cpp',
    'huffman/huff.cpp',
    'sparse/sparse.cpp'
)
$stormC = @(
    'jenkins/lookup3.c',
    'pklib/explode.c',
    'pklib/implode.c',
    'pklib/crc32.c',
    'LibTomCrypt.c',
    'LibTomMath.c',
    'LibTomMathDesc.c',
    'lzma/C/LzFind.c',
    'lzma/C/LzmaDec.c',
    'lzma/C/LzmaEnc.c',
    'zlib/adler32.c',
    'zlib/compress.c',
    'zlib/crc32.c',
    'zlib/deflate.c',
    'zlib/inffast.c',
    'zlib/inflate.c',
    'zlib/inftrees.c',
    'zlib/trees.c',
    'zlib/zutil.c',
    'bzip2/blocksort.c',
    'bzip2/bzlib.c',
    'bzip2/compress.c',
    'bzip2/crctable.c',
    'bzip2/decompress.c',
    'bzip2/huffman.c',
    'bzip2/randtable.c'
)
$stormFlags = @(
    '-I.',
    '-Ilibtomcrypt/src/headers',
    '-Ilibtommath',
    '-Ilzma/C',
    '-Izlib',
    '-Ibzip2',
    '-DUNICODE',
    '-D_UNICODE',
    '-D_7ZIP_ST',
    '-Wno-deprecated-non-prototype',
    '-Wno-implicit-function-declaration'
)
$stormObjects = @()
Push-Location (Join-Path $projectRoot 'vendor/stormlib')
try {
    foreach ($source in ($stormCpp + $stormC)) {
        $obj = Join-Path $buildDir ('storm_' + $source.Replace('/','_') + '.o')
        $compiler = if ($source.EndsWith('.cpp')) { $cxx } else { $cc }
        Compile $source $obj $vendorHeaders (@('-O2') + $stormFlags) $compiler
        $stormObjects += $obj
    }
    $storm = Join-Path $buildDir 'libstorm.a'
    $archiveNeeded = !(Test-Path $storm)
    if (!$archiveNeeded) {
        $stamp = (Get-Item $storm).LastWriteTimeUtc
        foreach ($obj in $stormObjects) { if ((Get-Item $obj).LastWriteTimeUtc -gt $stamp) { $archiveNeeded = $true; break } }
    }
    if ($archiveNeeded) { Invoke-Tool $ar (@('rcs', $storm) + $stormObjects) }
} finally { Pop-Location }
$objects += $storm
if ($Test) {
    $tests = @(Get-ChildItem (Join-Path $projectRoot 'tests') -Filter 'test_*.cpp' -File | Sort-Object Name)
    if ($TestFilter) { $tests = @($tests | Where-Object { $_.BaseName -eq $TestFilter }) }
    if (!$tests.Count) { throw 'No matching tests' }
    $testDir = Join-Path $buildDir 'test_objs'
    New-Item -ItemType Directory -Force $testDir | Out-Null
    $failed = @()
    foreach ($testSource in $tests) {
        $obj = Join-Path $testDir ($testSource.BaseName + '.o')
        $exe = Join-Path $testDir ($testSource.BaseName + '.exe')
        Compile $testSource.FullName $obj $testHeaders $cxxFlags $cxx
        Link $exe (@($obj) + $objects) @()
        Write-Host "Running $($testSource.BaseName)"
        $testArgs = @()
        if ($testSource.BaseName -in 'test_chat_decode','test_mpq_parser') { $testArgs = @('tests/fixtures/sample.SC2Replay') }
        if ($testSource.BaseName -eq 'test_real_replays') { $testArgs = @('tests/fixtures') }
        try { Invoke-Tool $exe $testArgs } catch { $failed += $testSource.BaseName; Write-Host $_ }
    }
    if ($failed.Count) { throw "Failed tests: $($failed -join ', ')" }
    Write-Host "PASS: $($tests.Count) tests"
    exit 0
}
$res = Join-Path $buildDir 'resource.o'
# Resource changes include the manifest and icon.
$resourceTime = (Get-ChildItem (Join-Path $projectRoot 'res') -File | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
if (!(Test-Path $res) -or (Get-Item $res).LastWriteTimeUtc -lt $resourceTime) {
    Invoke-Tool $windres @('res/resource.rc','-O','coff','-o',$res)
}
$target = Join-Path $buildDir 'sc2_coop.exe'
Link $target ($objects + @($res)) @('-mwindows')
New-Item -ItemType Directory -Force $outputDir | Out-Null
# Building does not stop or kill the user's running application.
Copy-Item -Force $target (Join-Path $outputDir 'sc2_coop.exe')
if (Test-Path (Join-Path $outputDir 'assets')) { Remove-Item -Recurse -Force (Join-Path $outputDir 'assets') }
Copy-Item -Recurse (Join-Path $projectRoot 'assets') $outputDir
Write-Host "Build complete: $outputDir"
if ($Package) { Package-Release }
