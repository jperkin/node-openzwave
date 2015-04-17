{
  "targets": [
    {
      "target_name": "openzwave",
      "sources": [
        "src/openzwave.cc"
      ],
      "include_dirs": [
        "deps/open-zwave/cpp/src"
        "deps/open-zwave/cpp/hidapi/hidapi",
        "deps/open-zwave/cpp/src",
        "deps/open-zwave/cpp/src/command_classes",
        "deps/open-zwave/cpp/src/platform",
        "deps/open-zwave/cpp/src/platform/unix",
        "deps/open-zwave/cpp/src/value_classes",
        "deps/open-zwave/cpp/tinyxml"
      ],
      "dependencies": [
        "deps/open-zwave/libopenzwave.gyp:libopenzwave"
      ],
      "configurations": {
        "Release": {
          "cflags": [
            "-Wno-ignored-qualifiers"
          ],
          "xcode_settings": {
            "OTHER_CFLAGS": [
              "-Wno-ignored-qualifiers"
            ]
          }
        }
      }
    }
  ]
}
