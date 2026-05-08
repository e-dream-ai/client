sources = [
  'src/test-cvec.c',
  'src/test-fmat.c',
  'src/test-fvec.c',
  'src/test-lvec.c',
  'src/test-mathutils-window.c',
  'src/test-mathutils.c',
  'src/test-vecutils.c',
  'src/utils/test-hist.c',
  'src/utils/test-log.c',
  'src/utils/test-parameter.c',
  'src/utils/test-scale.c',
  'src/spectral/test-awhitening.c',
  'src/spectral/test-dct.c',
  'src/spectral/test-fft.c',
  'src/spectral/test-filterbank.c',
  'src/spectral/test-filterbank_mel.c',
  'src/spectral/test-mfcc.c',
  'src/spectral/test-phasevoc.c',
  'src/spectral/test-specdesc.c',
  'src/spectral/test-tss.c',
  'src/io/test-sink.c',
  'src/io/test-sink_apple_audio.c',
  'src/io/test-sink_flac.c',
  'src/io/test-sink_sndfile.c',
  'src/io/test-sink_vorbis.c',
  'src/io/test-sink_wavwrite.c',
  'src/io/test-source.c',
  'src/io/test-source_apple_audio.c',
  'src/io/test-source_avcodec.c',
  'src/io/test-source_sndfile.c',
  'src/io/test-source_wavread.c',
  'src/onset/test-onset.c',
  'src/onset/test-peakpicker.c',
  'src/pitch/test-pitch.c',
  'src/pitch/test-pitchfcomb.c',
  'src/pitch/test-pitchmcomb.c',
  'src/pitch/test-pitchschmitt.c',
  'src/pitch/test-pitchspecacf.c',
  'src/pitch/test-pitchyin.c',
  'src/pitch/test-pitchyinfft.c',
  'src/notes/test-notes.c',
  'src/tempo/test-beattracking.c',
  'src/tempo/test-tempo.c',
  'src/temporal/test-a_weighting.c',
  'src/temporal/test-biquad.c',
  'src/temporal/test-c_weighting.c',
  'src/temporal/test-filter.c',
  'src/temporal/test-resampler.c',
  'src/effects/test-pitchshift.c',
  'src/effects/test-timestretch.c',
  'src/synth/test-sampler.c',
  'src/synth/test-wavetable.c',
]

test_file_path = '44100Hz_44100f_sine441_stereo.wav'
test_file_creation = custom_target ('create test file',
    input: 'create_tests_source.py',
    output: test_file_path,
    command: [find_program('python'), '@INPUT@', '@OUTPUT@'], install: false)

c_args += ['-DAUBIO_TESTS_SOURCE=' + test_file_creation[0].full_path()]
c_args += ['-DAUBIO_UNSTABLE_API=1']

foreach source : sources
  name = source.split('/')[-1].split('.')[0]
  t = executable (name, source, c_args : c_args, include_directories: '../src', link_with: aubio)
  test (name, t, depends: test_file_creation)
endforeach
