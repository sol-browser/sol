import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('static_audit',Path(__file__).resolve().parents[1]/'scripts/audit_static_platform_codecs.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
class StaticAuditTests(unittest.TestCase):
    def test_rejects_decoder_sources_and_archive_members(self):
        for name in ('../../third_party/ffmpeg/libavcodec/h264dec.c','obj/third_party/ffmpeg/ffmpeg_internal/aacdec.obj',r'obj\third_party\ffmpeg\ffmpeg_internal\aacdec_fixed.obj','../../third_party/ffmpeg/libavcodec/aac/aacdec_usac.c','ffmpeg_internal/autorename_libavcodec_aacdec.obj'):
            self.assertEqual(audit.forbidden([name]),[name])
    def test_preserves_container_parser_and_other_codec_inputs(self):
        self.assertEqual(audit.forbidden(['obj/ffmpeg/adts_parser.obj','obj/ffmpeg/mov.obj','obj/ffmpeg/mpegaudiodec_float.obj','../../media/formats/mp4/aac.cc','../../third_party/ffmpeg/libavformat/aacdec.c','ffmpeg_internal/autorename_libavformat_aacdec.obj']),[])
if __name__=='__main__':unittest.main()
