# Keep the old kernel compiler, but use the pinned current PS2 binutils.
# Legacy gas can corrupt expanded address stores with modern host libraries.
FROM ghcr.io/ps2dev/ps2dev@sha256:26832a6865b890927632734703e25faa24da1217dd1ea17dcedbddd7c7683be3 AS ps2-binutils
FROM ubuntu@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517
RUN dpkg --add-architecture i386 && apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    libc6:i386 libc6-dev gcc make patch perl binutils file ca-certificates && \
    rm -rf /var/lib/apt/lists/*
COPY --from=ps2-binutils /lib/ld-musl-x86_64.so.1 /lib/ld-musl-x86_64.so.1
COPY --from=ps2-binutils /usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-as /opt/psbbn-binutils/as.bin
COPY --from=ps2-binutils /usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-ld /opt/psbbn-binutils/ld
COPY --from=ps2-binutils /usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-ar /opt/psbbn-binutils/ar
COPY --from=ps2-binutils /usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-nm /opt/psbbn-binutils/nm
COPY --from=ps2-binutils /usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-objdump /opt/psbbn-binutils/objdump
COPY as-wrapper.sh /opt/psbbn-binutils/as
RUN sed -i 's/\r$//' /opt/psbbn-binutils/as && chmod +x /opt/psbbn-binutils/as && \
    /opt/psbbn-binutils/as --version && /opt/psbbn-binutils/ld --version
WORKDIR /work
