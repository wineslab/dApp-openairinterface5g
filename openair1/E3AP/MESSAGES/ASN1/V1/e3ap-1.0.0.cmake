set(E3AP_GRAMMAR ASN1/V1/e3ap-1.0.0.asn1)

set(e3ap_source
    ANY_aper.c
    ANY.c
    ANY_uper.c
    ANY_xer.c
    aper_decoder.c
    aper_encoder.c
    aper_opentype.c
    aper_support.c
    asn_application.c
    asn_bit_data.c
    asn_codecs_prim.c
    asn_codecs_prim_xer.c
    asn_internal.c
    asn_random_fill.c
    asn_SEQUENCE_OF.c
    asn_SET_OF.c
    ber_tlv_length.c
    ber_tlv_tag.c
    BIT_STRING.c
    BIT_STRING_print.c
    BIT_STRING_rfill.c
    BIT_STRING_uper.c
    BIT_STRING_xer.c
    constraints.c
    constr_CHOICE_aper.c
    constr_CHOICE.c
    constr_CHOICE_print.c
    constr_CHOICE_rfill.c
    constr_CHOICE_uper.c
    constr_CHOICE_xer.c
    constr_SEQUENCE_aper.c
    constr_SEQUENCE.c
    constr_SEQUENCE_OF_aper.c
    constr_SEQUENCE_OF.c
    constr_SEQUENCE_OF_uper.c
    constr_SEQUENCE_OF_xer.c
    constr_SEQUENCE_print.c
    constr_SEQUENCE_rfill.c
    constr_SEQUENCE_uper.c
    constr_SEQUENCE_xer.c
    constr_SET_OF_aper.c
    constr_SET_OF.c
    constr_SET_OF_print.c
    constr_SET_OF_rfill.c
    constr_SET_OF_uper.c
    constr_SET_OF_xer.c
    constr_TYPE.c
    E3-ControlAction.c
    E3-IndicationMessage.c
    E3-PDU.c
    E3-SetupRequest.c
    E3-SetupResponse.c
    ENUMERATED_aper.c
    ENUMERATED.c
    ENUMERATED_uper.c
    EXTERNAL.c
    GraphicString.c
    INTEGER_aper.c
    INTEGER.c
    INTEGER_print.c
    INTEGER_rfill.c
    INTEGER_uper.c
    INTEGER_xer.c
    NativeEnumerated_aper.c
    NativeEnumerated.c
    NativeEnumerated_uper.c
    NativeEnumerated_xer.c
    NativeInteger_aper.c
    NativeInteger.c
    NativeInteger_print.c
    NativeInteger_rfill.c
    NativeInteger_uper.c
    NativeInteger_xer.c
    ObjectDescriptor.c
    OBJECT_IDENTIFIER.c
    OBJECT_IDENTIFIER_print.c
    OBJECT_IDENTIFIER_rfill.c
    OBJECT_IDENTIFIER_xer.c
    OCTET_STRING_aper.c
    OCTET_STRING.c
    OCTET_STRING_print.c
    OCTET_STRING_rfill.c
    OCTET_STRING_uper.c
    OCTET_STRING_xer.c
    OPEN_TYPE_aper.c
    OPEN_TYPE.c
    OPEN_TYPE_uper.c
    OPEN_TYPE_xer.c
    per_decoder.c
    per_encoder.c
    per_opentype.c
    per_support.c
    uper_decoder.c
    uper_encoder.c
    uper_opentype.c
    uper_support.c
    xer_decoder.c
    xer_encoder.c
    xer_support.c
)

set(e3ap_headers
    ANY.h
    aper_decoder.h
    aper_encoder.h
    aper_opentype.h
    aper_support.h
    asn_application.h
    asn_bit_data.h
    asn_codecs.h
    asn_codecs_prim.h
    asn_config.h
    asn_internal.h
    asn_ioc.h
    asn_random_fill.h
    asn_SEQUENCE_OF.h
    asn_SET_OF.h
    asn_system.h
    ber_tlv_length.h
    ber_tlv_tag.h
    BIT_STRING.h
    constraints.h
    constr_CHOICE.h
    constr_SEQUENCE.h
    constr_SEQUENCE_OF.h
    constr_SET_OF.h
    constr_TYPE.h
    E3-ControlAction.h
    E3-IndicationMessage.h
    E3-PDU.h
    E3-SetupRequest.h
    E3-SetupResponse.h
    ENUMERATED.h
    EXTERNAL.h
    GraphicString.h
    INTEGER.h
    NativeEnumerated.h
    NativeInteger.h
    ObjectDescriptor.h
    OBJECT_IDENTIFIER.h
    OCTET_STRING.h
    OPEN_TYPE.h
    per_decoder.h
    per_encoder.h
    per_opentype.h
    per_support.h
    uper_decoder.h
    uper_encoder.h
    uper_opentype.h
    uper_support.h
    xer_decoder.h
    xer_encoder.h
    xer_support.h
)
