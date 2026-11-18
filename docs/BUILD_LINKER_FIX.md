# CANN link error: -ltiling_api / -lregister / -lplatform

Previous script linked three *operator registration / tiling / platform* libraries into the ASC kernel despite never using those host APIs:

```
/usr/bin/ld: cannot find -ltiling_api
/usr/bin/ld: cannot find -lregister
/usr/bin/ld: cannot find -lplatform
```

`ascend/CMakeLists.txt` removes those dependencies, specifies `LINKER_LANGUAGE ASC` and links the only directly used runtime (`libacl_rt.so` or `libascendcl.so`) by its discovered absolute path. The accompanying native Stratum executable is built separately from the ASC plugin, so network C++ builds even without the CANN SDK. This addresses the specific linker failure; it does *not* certify hardware-runtime compatibility with an installed CANN/driver/firmware combination.

Official reference: https://www.hiascend.com/doc_center/source/en/CANNCommunityEdition/900/programug/Ascendcopdevg/atlas_ascendc_10_00050.html (CMake section / Table 2; link the relevant libraries when those high-level APIs are used).
