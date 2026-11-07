"""Package the boot launcher and corresponding source."""
from pathlib import Path, PurePosixPath
import hashlib
import os
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
payload = ROOT / 'boot-dist'
source_archive = ROOT / 'build/psbbn-kernel-source.tar'
for line in (payload / 'SHA256SUMS').read_text().splitlines():
    expected, name = line.split(maxsplit=1)
    assert hashlib.sha256((payload / name).read_bytes()).hexdigest() == expected
with tarfile.open(source_archive) as archive:
    for item in archive:
        name = PurePosixPath(item.name)
        assert not name.is_absolute() and '..' not in name.parts
        assert name.suffix.lower() not in ('.elf', '.o', '.img', '.bin', '.gz', '.bz2', '.a'), item.name

binary_zip = ROOT / 'psbbn-mmce-boot-v0.6.12.zip'
with zipfile.ZipFile(binary_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('PSBBN/', b'')
    for file in sorted(payload.rglob('*')):
        if file.is_file():
            assert 'private' not in file.relative_to(payload).parts
            archive.write(file, file.relative_to(payload).as_posix())

source_zip = ROOT / 'psbbn-mmce-boot-v0.6.12-source.zip'
suffixes = {'.c', '.h', '.S', '.py', '.sh', '.ps1', '.mk', '.md', '.txt', '.lst', '.tab', '.patch'}
excluded = {'private', '.upstream', 'build', 'dist', 'boot-dist', '__pycache__', 'psbbn-mmce-boot-v0.6'}
with zipfile.ZipFile(source_zip, 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.write(source_archive, 'build/psbbn-kernel-source.tar')
    for directory, directories, files in os.walk(ROOT):
        directories[:] = sorted(name for name in directories
                                if name not in excluded and not name.startswith('psbbn-mmce-boot-v'))
        for name in sorted(files):
            file = Path(directory) / name
            relative = file.relative_to(ROOT)
            if file.suffix in suffixes or file.name.startswith('Makefile') or file.name in (
                    'Dockerfile', 'LICENSE', '.gitignore', '.gitattributes', '.dockerignore', 'probe.dat'):
                archive.write(file, relative.as_posix())
for file in (binary_zip, source_zip):
    with zipfile.ZipFile(file) as archive:
        assert archive.testzip() is None
    print(f'{file.name}: {file.stat().st_size} bytes; SHA256 {hashlib.sha256(file.read_bytes()).hexdigest()}')
