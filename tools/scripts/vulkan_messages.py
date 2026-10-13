"""The validation pre-pass every benchmark script runs before it measures anything (RT2-1).

A benchmark of a frame the validation layer objects to is a number about undefined behaviour. So before
measuring a scene, a script runs it for a few frames with the layers on (`--validation=on`, which also turns
on synchronization validation) and refuses to report numbers while any message remains.

The engine logs a layer error or warning as "[Vulkan] ..." and the layer's notes as "[Vulkan note] ...", so
any "[Vulkan]" line is a problem -- except the layer's own advisory that it adjusted its settings, which says
what it changed, not what the engine did wrong.
"""
import subprocess

ADVISORIES = ('Warning that validation is adjusting settings',)


def vulkan_messages(log):
    """The problem lines in a run's log."""
    return [line for line in log.splitlines()
            if '[Vulkan]' in line and not any(a in line for a in ADVISORIES)]


def validate(command, cwd, timeout=1800):
    """Run `command` (an engine launch without --benchmark) for three frames under the validation layers.
    Returns the problem lines; a run that did not finish cleanly is one too."""
    p = subprocess.run(list(command) + ['--validation=on', '--benchmark=3'], cwd=cwd, capture_output=True,
                       text=True, errors='replace', timeout=timeout)
    problems = vulkan_messages(p.stdout + p.stderr)
    if p.returncode != 0:
        problems.append('exit code %d (3 means a shader or GPU-layer failure)' % p.returncode)
    return problems


if __name__ == '__main__':
    assert vulkan_messages('[1] RageV: [Vulkan note] hi\n[2] RageV: [Vulkan] VUID-x bad\n') == ['[2] RageV: [Vulkan] VUID-x bad']
    assert vulkan_messages('[1] RageV: [Vulkan] vkCreateDevice(): Warning that validation is adjusting settings:') == []
    print('ok')
