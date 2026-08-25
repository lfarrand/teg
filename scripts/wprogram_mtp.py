"""Stop Teensyduino 1.62 WProgram.h from including MTP (FS.h include cycle)."""

MTP_INCLUDE = '#include "MTP_Teensy.h"\n'
FS_BEFORE_MTP = '#include "FS.h" // TEG: complete FS before core MTP headers\n'
NO_MTP = "// TEG: do not include MTP here; FS.h includes Arduino.h\n"


def without_wprogram_mtp(text: str) -> str:
    text = text.replace(FS_BEFORE_MTP, "")
    if NO_MTP in text and MTP_INCLUDE not in text:
        return text
    if MTP_INCLUDE not in text:
        raise ValueError("WProgram.h does not include MTP_Teensy.h")
    return text.replace(MTP_INCLUDE, NO_MTP, 1)
