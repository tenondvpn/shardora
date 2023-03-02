  
{
  'variables': {
    'target_arch%': '<!(node -p "process.arch")>'
  },
  'target_default': {
    'cflags': [
      '-Wall',
      '-Wextra',
    ],
  },
  'targets': [
    {
      'target_name': 'secp256k1',
      'sources': [
        'secp256k1/src/secp256k1.c',
        'secp256k1/src',
        'binding.c'
      ],
      'include_dirs': [
        'secp256k1',
        'secp256k1/src',
        'secp256k1/include'
      ],
      'xcode_settings': {
        'OTHER_CFLAGS': [
          '-g',
          '-O3',
          '-Wall',
          '-pedantic-errors',
        ]
      },
      'cflags': [
        '-g',
        '-O3',
        '-Wall',
        '-pedantic-errors'
      ],
      'defines': [
        'ECMULT_GEN_PREC_BITS=4',
        'ECMULT_WINDOW_SIZE=15',
        # Activate modules
        'ENABLE_MODULE_ECDH=1',
        'ENABLE_MODULE_RECOVERY=1',
        #
        'USE_ENDOMORPHISM=1',
        # Ignore GMP, dynamic linking, so will be hard to use with prebuilds
        'USE_NUM_NONE=1',
        'USE_FIELD_INV_BUILTIN=1',
        'USE_SCALAR_INV_BUILTIN=1'
      ],
      'conditions': [
        ['target_arch=="x64" and OS!="win"', {
          'defines': [
            'HAVE___INT128=1',
            'USE_ASM_X86_64=1',
            'USE_FIELD_5X52=1',
            'USE_SCALAR_4X64=1',
          ]
        }, {
          'defines': [
            'USE_FIELD_10X26=1',
            'USE_SCALAR_8X32=1',
          ]
        }],
        ['OS != "mac" and OS != "win"', {
          'link_settings': {
            'libraries': [ "-Wl,-rpath=\\$$ORIGIN"]
          }
        }]
      ],
    }
  ],
}


