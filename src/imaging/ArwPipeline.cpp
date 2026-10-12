// ArwDecoder and ArwPreviewSession intentionally share one translation unit so
// the interactive preview can reuse the decoder's private linear develop pipeline
// without exposing those implementation details as public API or duplicating them.
#include "ArwDecoder.cpp"
#include "ArwPreviewSessionImpl.inc"
