# this code is from: https://github.com/pytorch/audio/blob/main/test/torchaudio_unittest/backend/soundfile/info_test.py
import paddle
import os.path
import tempfile
import unittest
import scipy

from paddle.audio.backends import soundfile_backend
from typing import Optional

def normalize_wav(tensor: paddle.Tensor) -> paddle.Tensor:
    if tensor.dtype == paddle.float32:
        pass
    elif tensor.dtype == paddle.int32:
        tensor = paddle.cast(tensor, paddle.float32)
        tensor[tensor > 0] /= 2147483647.0
        tensor[tensor < 0] /= 2147483648.0
    elif tensor.dtype == paddle.int16:
        tensor = paddle.cast(tensor, paddle.float32)
        tensor[tensor > 0] /= 32767.0
        tensor[tensor < 0] /= 32768.0
    elif tensor.dtype == paddle.uint8:
        tensor = paddle.cast(tensor, paddle.float32) - 128
        tensor[tensor > 0] /= 127.0
        tensor[tensor < 0] /= 128.0
    return tensor

def get_wav_data(
    dtype: str,
    num_channels: int,
    *,
    num_frames: Optional[int] = None,
    normalize: bool = True,
    channels_first: bool = True,
):
    """Generate linear signal of the given dtype and num_channels

    Data range is
        [-1.0, 1.0] for float32,
        [-2147483648, 2147483647] for int32
        [-32768, 32767] for int16
        [0, 255] for uint8

    num_frames allow to change the linear interpolation parameter.
    Default values are 256 for uint8, else 1 << 16.
    1 << 16 as default is so that int16 value range is completely covered.
    """
    dtype_ = getattr(paddle, dtype)

    if num_frames is None:
        if dtype == "uint8":
            num_frames = 256
        else:
            num_frames = 1 << 16

    # paddle linspace not support uint8, int8, int16
    #if dtype == "uint8":
    #    base = paddle.linspace(0, 255, num_frames, dtype=dtype_)
         #dtype_np = getattr(np, dtype)
         #base_np = np.linspace(0, 255, num_frames, dtype_np)
         #base = paddle.to_tensor(base_np, dtype=dtype_)
    #elif dtype == "int8":
    #    base = paddle.linspace(-128, 127, num_frames, dtype=dtype_)
         #dtype_np = getattr(np, dtype)
         #base_np = np.linspace(-128, 127, num_frames, dtype_np)
         #base = paddle.to_tensor(base_np, dtype=dtype_)
    if dtype == "float32":
        base = paddle.linspace(-1.0, 1.0, num_frames, dtype=dtype_)
    elif dtype == "float64":
        base = paddle.linspace(-1.0, 1.0, num_frames, dtype=dtype_)
    elif dtype == "int32":
        base = paddle.linspace(-2147483648, 2147483647, num_frames, dtype=dtype_)
    #elif dtype == "int16":
    #    base = paddle.linspace(-32768, 32767, num_frames, dtype=dtype_)
         #dtype_np = getattr(np, dtype)
         #base_np = np.linspace(-32768, 32767, num_frames, dtype_np)
         #base = paddle.to_tensor(base_np, dtype=dtype_)
    else:
        raise NotImplementedError(f"Unsupported dtype {dtype}")
    data = base.tile([num_channels, 1])
    if not channels_first:
        data = data.transpose([1, 0])
    if normalize:
        data = normalize_wav(data)
    return data

def save_wav(path, data, sample_rate, channels_first=True):
    """Save wav file without paddleaudio"""
    if channels_first:
        data = data.transpose([1, 0])
    scipy.io.wavfile.write(path, sample_rate, data.numpy())

class TempDirMixin:
    """Mixin to provide easy access to temp dir"""
    temp_dir_ = None

    @classmethod
    def get_base_temp_dir(cls):
        # If PADDLEAUDIO_TEST_TEMP_DIR is set, use it instead of temporary directory.
        # this is handy for debugging.
        key = "PADDLEAUDIO_TEST_TEMP_DIR"
        if key in os.environ:
            return os.environ[key]
        if cls.temp_dir_ is None:
            cls.temp_dir_ = tempfile.TemporaryDirectory()
        return cls.temp_dir_.name

    @classmethod
    def tearDownClass(cls):
        if cls.temp_dir_ is not None:
            try:
                cls.temp_dir_.cleanup()
                cls.temp_dir_ = None
            except PermissionError:
                # On Windows there is a know issue with `shutil.rmtree`,
                # which fails intermittenly.
                #
                # https://github.com/python/cpython/issues/74168
                #
                # We observed this on CircleCI, where Windows job raises
                # PermissionError.
                #
                # Following the above thread, we ignore it.
                pass
        super().tearDownClass()

    def get_temp_path(self, *paths):
        temp_dir = os.path.join(self.get_base_temp_dir(), self.id())
        path = os.path.join(temp_dir, *paths)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        return path

def get_bit_depth(dtype):
    bit_depths = {
        "float32": 32,
        "int32": 32,
        "int16": 16,
        "uint8": 8,
    }
    return bit_depths[dtype]

def get_bits_per_sample(ext, dtype):
    bits_per_samples = {
        "flac": 24,
        "mp3": 0,
        "vorbis": 0,
    }
    return bits_per_samples.get(ext, get_bit_depth(dtype))

def get_encoding(ext, dtype):
    exts = {
        "mp3",
        "flac",
        "vorbis",
    }
    encodings = {
        "float32": "PCM_F",
        "int32": "PCM_S",
        "int16": "PCM_S",
        "uint8": "PCM_U",
    }
    return ext.upper() if ext in exts else encodings[dtype]

class TestAudioBackends(TempDirMixin, unittest.TestCase):

    def test_info(self):
        """`soundfile_backend.info` can check wav file correctly"""
        duration = 1
        num_channels = 2
        sample_rate = 16000
        dtype = "float32"
        path = self.get_temp_path("data.wav")
        data = get_wav_data(dtype, num_channels, normalize=False, num_frames=duration * sample_rate)
        save_wav(path, data, sample_rate)
        info = soundfile_backend.info(path)
        assert info.sample_rate == sample_rate
        assert info.num_frames == sample_rate * duration
        assert info.num_channels == num_channels
        assert info.bits_per_sample == get_bits_per_sample("wav", dtype)
        assert info.encoding == get_encoding("wav", dtype)

if __name__ == "__main__":
    unittest.main()
