; NOTE: Assertions have been autogenerated by utils/update_test_checks.py UTC_ARGS: --function-signature --scrub-attributes --check-attributes
; RUN: opt -attributor -enable-new-pm=0 -attributor-manifest-internal  -attributor-max-iterations-verify -attributor-annotate-decl-cs -attributor-max-iterations=1 -S < %s | FileCheck %s --check-prefixes=CHECK,NOT_CGSCC_NPM,NOT_CGSCC_OPM,NOT_TUNIT_NPM,IS__TUNIT____,IS________OPM,IS__TUNIT_OPM
; RUN: opt -aa-pipeline=basic-aa -passes=attributor -attributor-manifest-internal  -attributor-max-iterations-verify -attributor-annotate-decl-cs -attributor-max-iterations=1 -S < %s | FileCheck %s --check-prefixes=CHECK,NOT_CGSCC_OPM,NOT_CGSCC_NPM,NOT_TUNIT_OPM,IS__TUNIT____,IS________NPM,IS__TUNIT_NPM
; RUN: opt -attributor-cgscc -enable-new-pm=0 -attributor-manifest-internal  -attributor-annotate-decl-cs -S < %s | FileCheck %s --check-prefixes=CHECK,NOT_TUNIT_NPM,NOT_TUNIT_OPM,NOT_CGSCC_NPM,IS__CGSCC____,IS________OPM,IS__CGSCC_OPM
; RUN: opt -aa-pipeline=basic-aa -passes=attributor-cgscc -attributor-manifest-internal  -attributor-annotate-decl-cs -S < %s | FileCheck %s --check-prefixes=CHECK,NOT_TUNIT_NPM,NOT_TUNIT_OPM,NOT_CGSCC_OPM,IS__CGSCC____,IS________NPM,IS__CGSCC_NPM

%struct.wobble = type { i32 }
%struct.zot = type { %struct.wobble, %struct.wobble, %struct.wobble }

declare dso_local fastcc float @bar(%struct.wobble* noalias, <8 x i32>) unnamed_addr

define %struct.zot @widget(<8 x i32> %arg) local_unnamed_addr {
; IS__TUNIT____: Function Attrs: nofree nosync nounwind readnone willreturn
; IS__TUNIT____-LABEL: define {{[^@]+}}@widget
; IS__TUNIT____-SAME: (<8 x i32> [[ARG:%.*]]) local_unnamed_addr
; IS__TUNIT____-NEXT:  bb:
; IS__TUNIT____-NEXT:    ret [[STRUCT_ZOT:%.*]] undef
;
; IS__CGSCC____: Function Attrs: nofree norecurse nosync nounwind readnone willreturn
; IS__CGSCC____-LABEL: define {{[^@]+}}@widget
; IS__CGSCC____-SAME: (<8 x i32> [[ARG:%.*]]) local_unnamed_addr
; IS__CGSCC____-NEXT:  bb:
; IS__CGSCC____-NEXT:    ret [[STRUCT_ZOT:%.*]] undef
;
bb:
  ret %struct.zot undef
}

define void @baz(<8 x i32> %arg) local_unnamed_addr {
; IS__TUNIT____: Function Attrs: nofree nosync nounwind readnone willreturn
; IS__TUNIT____-LABEL: define {{[^@]+}}@baz
; IS__TUNIT____-SAME: (<8 x i32> [[ARG:%.*]]) local_unnamed_addr
; IS__TUNIT____-NEXT:  bb:
; IS__TUNIT____-NEXT:    ret void
;
; IS__CGSCC____: Function Attrs: nofree norecurse nosync nounwind readnone willreturn
; IS__CGSCC____-LABEL: define {{[^@]+}}@baz
; IS__CGSCC____-SAME: (<8 x i32> [[ARG:%.*]]) local_unnamed_addr
; IS__CGSCC____-NEXT:  bb:
; IS__CGSCC____-NEXT:    [[TMP1:%.*]] = extractvalue [[STRUCT_ZOT:%.*]] undef, 0, 0
; IS__CGSCC____-NEXT:    ret void
;
bb:
  %tmp = call %struct.zot @widget(<8 x i32> %arg)
  %tmp1 = extractvalue %struct.zot %tmp, 0, 0
  ret void
}
