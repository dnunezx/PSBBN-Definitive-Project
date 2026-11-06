$ErrorActionPreference = 'Stop'
$experimentRoot = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $experimentRoot '.upstream/psbbn-kernel'
$cacheDir = Join-Path $experimentRoot 'private'
$buildDir = Join-Path $experimentRoot 'build'
$revision = '80b14105512ed6114c1defa8e9dee40679072874'
New-Item -ItemType Directory -Force -Path $cacheDir,$buildDir | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $sourceDir '.git'))) {
    git clone --no-checkout https://github.com/CosmicScale/PSBBN-Definitive-Patch-Kernel.git $sourceDir
    if ($LASTEXITCODE) { throw 'Kernel source download failed' }
}
git -C $sourceDir cat-file -e "${revision}^{commit}"
if ($LASTEXITCODE) { throw 'Pinned kernel source revision unavailable' }
$sourceArchive = Join-Path $buildDir 'psbbn-kernel-source.tar'
git -C $sourceDir archive --format=tar "--output=$sourceArchive" $revision
if ($LASTEXITCODE) { throw 'Kernel source archive failed' }
$archives = @{
    'binutils-2.9EE-cross.tar.gz' = 'e3990ee12125ae63c703d54c5a834f79a83be9f7daa834e801081da81c60af8a'
    'gcc-2.95.2-cross.tar.gz' = '2970813c5e5bdae69019a3b416f09c1a7dbcb69c8e523118e55ec77b9abcd373'
}
foreach ($archive in $archives.Keys) {
    $archivePath = Join-Path $cacheDir $archive
    if (-not (Test-Path -LiteralPath $archivePath) -or (Get-FileHash -LiteralPath $archivePath).Hash -ne $archives[$archive]) {
        Invoke-WebRequest -Uri "https://ps2-linux.tensioncore.com/download/ps2stuff/$archive" -OutFile $archivePath
    }
    if ((Get-FileHash -LiteralPath $archivePath).Hash -ne $archives[$archive]) { throw "Archive hash mismatch: $archive" }
}
Write-Output 'Pinned source and compiler archives prepared. No PSBBN image modified.'
