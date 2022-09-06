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
import unittest

import soundfile
import numpy as np
import os
import paddle.audio
import itertools
from parameterized import parameterized


def parameterize(*params):
    return parameterized.expand(list(itertools.product(*params)))


class TestAudioDatasets(unittest.TestCase):

    def setUp(self):
        self.initParmas()

    def initParmas(self):

        def get_wav_data(dtype: str, num_channels: int, num_frames: int):
            dtype_ = getattr(paddle, dtype)
            base = paddle.linspace(-1.0, 1.0, num_frames, dtype=dtype_) * 0.1
            data = base.tile([num_channels, 1])
            return data

        self.duration = 0.5
        self.num_channels = 1
        self.sr = 16000
        self.dtype = "float32"
        self.window_size = 1024
        waveform_tensor = get_wav_data(self.dtype,
                                       self.num_channels,
                                       num_frames=self.duration * self.sr)
        self.waveform = waveform_tensor.numpy()

    def test_backend(self):
        base_dir = os.getcwd()
        wave_wav_path = os.path.join(base_dir, "wave_test.wav")
        paddle.audio.backends.save(wave_wav_path,
                                   paddle.to_tensor(self.waveform),
                                   self.sr,
                                   channels_first=False)

        # test backends(wave)(wave_backend) info
        wav_info = paddle.audio.backends.info(wave_wav_path)
        self.assertTrue(wav_info.sample_rate, self.sr)
        self.assertTrue(wav_info.num_channels, self.num_channels)
        self.assertTrue(wav_info.bits_per_sample, 16)

        # test backends(wave_backend) load & save
        wav_data, sr = paddle.audio.backends.load(wave_wav_path)
        np.testing.assert_array_almost_equal(wav_data, self.waveform, decimal=4)
        with soundfile.SoundFile(wave_wav_path, "r") as file_:
            dtype = "float32"
            frames = file_._prepare_read(0, None, -1)
            waveform = file_.read(frames, dtype, always_2d=True)
            waveform = waveform.T
            np.testing.assert_array_almost_equal(wav_data, waveform)
        if os.path.exists(wave_wav_path):
            os.remove(wave_wav_path)

    @parameterize(["dev", "train"], [40, 64])
    def test_tess_dataset(self, mode: str, params: int):
        tess_dataset = paddle.audio.datasets.TESS(mode=mode,
                                                  feat_type='mfcc',
                                                  n_mfcc=params)
        idx = np.random.randint(0, 500)
        elem = tess_dataset[idx]
        self.assertTrue(elem[0].shape[1] == params)
        self.assertTrue(0 <= elem[1] <= 6)

        tess_dataset = paddle.audio.datasets.TESS(mode=mode,
                                                  feat_type='spectrogram',
                                                  n_fft=params)
        elem = tess_dataset[idx]
        self.assertTrue(elem[0].shape[1] == (params // 2 + 1))
        self.assertTrue(0 <= elem[1] <= 6)

        tess_dataset = paddle.audio.datasets.TESS(mode="dev",
                                                  feat_type='logmelspectrogram',
                                                  n_mels=params)
        elem = tess_dataset[idx]
        self.assertTrue(elem[0].shape[1] == params)
        self.assertTrue(0 <= elem[1] <= 6)

        tess_dataset = paddle.audio.datasets.TESS(mode="dev",
                                                  feat_type='melspectrogram',
                                                  n_mels=params)
        elem = tess_dataset[idx]
        self.assertTrue(elem[0].shape[1] == params)
        self.assertTrue(0 <= elem[1] <= 6)


if __name__ == '__main__':
    unittest.main()
