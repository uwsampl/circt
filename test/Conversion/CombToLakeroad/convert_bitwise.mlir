//RUN: circt-opt %s --lower-comb-to-lakeroad | FileCheck %s
// CHECK: module { 
// CHECK: hw.module.extern @lakeroad_and_1_2(%i0: i1, %i1: i1) -> (out: i1) 
// CHECK: hw.module.extern @lakeroad_or_1_2(%i0: i1, %i1: i1) -> (out: i1) 
// CHECK: hw.module.extern @lakeroad_xor_1_2(%i0: i1, %i1: i1) -> (out: i1) 
// CHECK: hw.module @test(%lhs: i1, %rhs: i1) -> (o: i1) { 
hw.module @test(%lhs : i1, %rhs : i1) -> (o: i1){
  // CHECK: %lakeroad_generated_instance.out = hw.instance "lakeroad_generated_instance" @lakeroad_and_1_2(i0: %lhs: i1, i1: %rhs: i1) -> (out: i1)
  %1 = comb.and %lhs, %rhs : i1
  // CHECK: %lakeroad_generated_instance.out_0 = hw.instance "lakeroad_generated_instance" @lakeroad_or_1_2(i0: %lhs: i1, i1: %rhs: i1) -> (out: i1)
  %2 = comb.or %lhs, %rhs : i1
  // CHECK: %lakeroad_generated_instance.out_1 = hw.instance "lakeroad_generated_instance" @lakeroad_xor_1_2(i0: %lakeroad_generated_instance.out: i1, i1: %lakeroad_generated_instance.out_0: i1) -> (out: i1)
  %3 = comb.xor %1, %2 : i1
  // CHECK: hw.output %lakeroad_generated_instance.out_1 : i1
  hw.output %3 : i1
}