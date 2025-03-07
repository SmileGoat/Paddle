# Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
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
import paddle
import paddle.fluid.layers as layers
from paddle.fluid.executor import Executor
import paddle.fluid.core as core
import paddle.fluid as fluid
from paddle.fluid.backward import append_backward
import numpy
from paddle.fluid import compiler, Program, program_guard

paddle.enable_static()


class TestWhileOp(unittest.TestCase):
    def simple_net(self):
        d0 = paddle.static.data(
            "d0", shape=[10], dtype='float32'
        )
        d1 = paddle.static.data(
            "d1", shape=[10], dtype='float32'
        )
        d2 = paddle.static.data(
            "d2", shape=[10], dtype='float32'
        )
        # fill_constant npu op doesn't support int64
        i = layers.zeros(shape=[1], dtype='int32')
        i = layers.cast(i, 'int64')
        i.stop_gradient = True
        init = layers.zeros(shape=[10], dtype='float32')
        mem_array = paddle.tensor.array_write(x=init, i=i)
        data_array = paddle.tensor.array_write(x=d0, i=i)
        i = paddle.increment(i)
        paddle.tensor.array_write(d1, i, array=data_array)
        i = paddle.increment(i)
        paddle.tensor.array_write(d2, i, array=data_array)
        i = layers.zeros(shape=[1], dtype='int32')
        i = layers.cast(i, 'int64')
        i.stop_gradient = True
        array_len = layers.fill_constant(shape=[1], dtype='int32', value=5)
        array_len = layers.cast(array_len, 'int64')
        array_len.stop_gradient = True
        cond = paddle.ones(shape=[1], dtype='int32')
        cond = layers.cast(cond, 'bool')
        j = layers.fill_constant(shape=[1], dtype='int32', value=1)
        j = layers.cast(j, 'int64')
        j.stop_gradient = True
        array_len2 = layers.fill_constant(shape=[1], dtype='int32', value=3)
        array_len2 = layers.cast(array_len2, 'int64')
        array_len2.stop_gradient = True
        cond2 = paddle.logical_or(x=j, y=array_len2)
        cond2 = paddle.ones(shape=[1], dtype='int32')
        cond2 = layers.cast(cond2, 'bool')
        while_op = paddle.static.nn.control_flow.While(cond=cond)
        while_op2 = paddle.static.nn.control_flow.While(cond=cond2)
        with while_op.block():
            d = paddle.tensor.array_read(array=data_array, i=i)
            prev = paddle.tensor.array_read(array=mem_array, i=i)
            result = layers.sums(input=[d, prev])

            i = paddle.increment(x=i)
            paddle.tensor.array_write(result, i=i, array=mem_array)
            paddle.assign(paddle.less_than(x=i, y=array_len), cond)

            with while_op2.block():
                d2 = paddle.tensor.array_read(array=data_array, i=j)
                prev2 = paddle.tensor.array_read(array=mem_array, i=j)
                result2 = layers.sums(input=[d2, prev2])

                j = paddle.increment(x=j)
                paddle.tensor.array_write(result2, i=j, array=mem_array)
                paddle.assign(paddle.less_than(x=j, y=array_len2), cond2)
        sum_result = paddle.tensor.array_read(array=mem_array, i=j)
        loss = paddle.mean(sum_result)
        return loss, sum_result

    def test_simple_net(self):
        paddle.enable_static()
        main_program = fluid.Program()
        startup_program = fluid.Program()
        with fluid.program_guard(main_program, startup_program):
            loss, sum_result = self.simple_net()

            append_backward(loss)

            npu_place = paddle.NPUPlace(0)
            exe = Executor(npu_place)
            d = []

            for i in range(3):
                d.append(numpy.random.random(size=[10]).astype('float32'))

            outs = exe.run(
                feed={'d0': d[0], 'd1': d[1], 'd2': d[2]},
                fetch_list=[sum_result],
            )
            self.assertAlmostEqual(numpy.sum(d), numpy.sum(outs[0]), delta=0.01)

    def test_simple_net_forward(self):
        paddle.enable_static()
        main_program = fluid.Program()
        startup_program = fluid.Program()
        with fluid.program_guard(main_program, startup_program):
            self.simple_net()

            npu_place = paddle.NPUPlace(0)
            exe = Executor(npu_place)
            d = []

            for i in range(3):
                d.append(numpy.random.random(size=[10]).astype('float32'))

            for _ in range(2):
                exe.run(main_program, feed={'d0': d[0], 'd1': d[1], 'd2': d[2]})


if __name__ == '__main__':
    unittest.main()
