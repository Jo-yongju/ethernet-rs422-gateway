# Common protocol mirror

The canonical Protocol V1 implementation is in the repository-level `common/`
directory. These six files are an exact mirror used by the standalone
STM32CubeIDE project because the canonical directory is outside that project.

Do not make F429-specific changes here. Update the canonical files first, run
the host regression tests, and then refresh this mirror byte-for-byte.
