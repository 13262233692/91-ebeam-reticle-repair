{
  "targets": [
    {
      "target_name": "ebeam-repair",
      "sources": [
        "src/addon.cpp",
        "src/image_processor.cpp",
        "src/scan_path_generator.cpp",
        "src/dose_matrix_builder.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "src/include",
        "C:/opencv/build/include",
        "C:/opencv/build/include/opencv2"
      ],
      "libraries": [
        "-LC:/opencv/build/x64/vc16/lib",
        "opencv_world480.lib"
      ],
      "defines": [
        "NAPI_CPP_EXCEPTIONS",
        "OPENCV_MUTABLE_MATRIX",
        "NAPI_DISABLE_CPP_EXCEPTIONS"
      ],
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "cflags_cc": [
        "/std:c++17",
        "/EHsc",
        "/O2",
        "/arch:AVX2"
      ],
      "conditions": [
        ["OS==\"win\"", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "AdditionalOptions": [
                "/std:c++17",
                "/O2",
                "/Oi",
                "/GL",
                "/arch:AVX2",
                "/fp:fast"
              ]
            },
            "VCLinkerTool": {
              "AdditionalOptions": [
                "/LTCG:incremental"
              ]
            }
          }
        }],
        ["OS==\"linux\"", {
          "include_dirs+": ["/usr/include/opencv4"],
          "libraries+": [
            "-lopencv_core",
            "-lopencv_imgproc",
            "-lopencv_imgcodecs"
          ],
          "cflags_cc+": [
            "-std=c++17",
            "-O3",
            "-mavx2",
            "-mfma",
            "-ffast-math"
          ]
        }],
        ["OS==\"mac\"", {
          "include_dirs+": ["/usr/local/include/opencv4"],
          "libraries+": [
            "-lopencv_core",
            "-lopencv_imgproc",
            "-lopencv_imgcodecs"
          ],
          "cflags_cc+": [
            "-std=c++17",
            "-O3",
            "-mavx2",
            "-ffast-math"
          ]
        }]
      ]
    }
  ]
}
