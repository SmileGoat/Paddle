# Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import sys
import warnings
from . import wave_backend
from . import backend
from typing import List

import paddle

__all__ = [
    'get_current_audio_backend', 'list_available_backends', 'set_backend'
]


def _check_version(version: str) -> bool:
    # require paddleaudio >= 1.0.2
    ver_arr = version.split('.')
    v0 = int(ver_arr[0])
    v1 = int(ver_arr[1])
    v2 = int(ver_arr[2])
    if v0 < 1:
        return False
    if v0 == 1 and v1 == 0 and v2 <= 1:
        return False
    return True


def list_available_backends() -> List[str]:
    """ List available backends, the backends in paddleaudio and
        the default backend.

    Returns:
        List[str]: The list of available backends.

    Examples:
        .. code-block:: python

            import paddle
            backends = paddle.audio.backends.list_available_backends()
            # return ['wave_backend']
            # return ['wave_backend', 'soundfile'], if have installed paddleaudio >= 1.0.2
    """
    backends = []
    try:
        import paddleaudio
    except ImportError:
        package = "paddleaudio"
        warn_msg = (
            "Failed importing {}. \n"
            "only wave_banckend(only can deal with PCM16 WAV) supportted.\n"
            "if want soundfile_backend(more audio type suppported),\n"
            "please manually installed (usually with `pip install {} >= 1.0.2`). "
        ).format(package, package)
        warnings.warn(warn_msg)

    if "paddleaudio" in sys.modules:
        version = paddleaudio.__version__
        if _check_version(version) == False:
            err_msg = (
                "the version of paddleaudio installed is {},\n"
                "please ensure the paddleaudio >= 1.0.2.").format(version)
            raise ImportError(err_msg)
        backends = paddleaudio.backends.list_audio_backends()
    backends.append("wave_backend")
    return backends


def get_current_audio_backend() -> str:
    """ Get the name of the current audio backend

    Returns:
        str: The name of the current backend,
        the wave_backend or backend imported from paddleaudio

    Examples:
        .. code-block:: python

            import paddle
            backends = paddle.audio.backends.get_current_audio_backend()
            # wave_backend or soundfile
    """
    current_backend = None
    if "paddleaudio" in sys.modules:
        import paddleaudio
        current_backend = paddleaudio.backends.get_audio_backend()
        if backend.load == paddleaudio.load:
            return current_backend
    return "wave_backend"


def set_backend(backend_name: str):
    """Set the backend by one of the list_audio_backend return.

    Args:
        backend (str): one of the list_audio_backend.
        "wave_backend" is the default.
        "soundfile" imported from paddleaudio.

    Examples:
        .. code-block:: python

            import paddle
            paddle.audio.backends.set_backend('wave_backend')
            # if have installed paddleaudio >= 1.0.2
            paddle.audio.backends.set_backend('soundfile')
    """
    if backend_name not in list_available_backends():
        raise NotImplementedError()

    if backend_name is "wave_backend":
        module = wave_backend
    else:
        import paddleaudio
        paddleaudio.backends.set_audio_backend(backend_name)
        module = paddleaudio

    for func in ["save", "load", "info"]:
        setattr(backend, func, getattr(module, func))
        setattr(paddle.audio.backends, func, getattr(module, func))


def _init_set_audio_backend():
    # init the default wave_backend.
    for func in ["save", "load", "info"]:
        setattr(backend, func, getattr(wave_backend, func))
