/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef H_BLE_GATT_
#define H_BLE_GATT_

/**
 * @brief Bluetooth Generic Attribute Profile (GATT)
 * @defgroup bt_gatt Bluetooth Generic Attribute Profile (GATT)
 * @ingroup bt_host
 * @{
 */

#include <inttypes.h>
#include "host/ble_att.h"
#include "host/ble_uuid.h"
#ifdef __cplusplus
extern "C" {
#endif

struct ble_hs_conn;
struct ble_att_error_rsp;
struct ble_hs_cfg;

/**
 * @defgroup ble_gatt_register_op_codes Generic Attribute Profile (GATT) Registration Operation Codes
 * @{
 */

/** GATT Service registration. */
#define BLE_GATT_REGISTER_OP_SVC                        1

/** GATT Characteristic registration. */
#define BLE_GATT_REGISTER_OP_CHR                        2

/** GATT Descriptor registration. */
#define BLE_GATT_REGISTER_OP_DSC                        3

/** @} */

/**
 * @defgroup ble_gatt_uuid Generic Attribute Profile (GATT) Service and Descriptor UUIDs
 * @{
 */

/** GATT service 16-bit UUID. */
#define BLE_GATT_SVC_UUID16                             0x1801

/** GATT Client Characteristic Configuration descriptor 16-bit UUID. */
#define BLE_GATT_DSC_CLT_CFG_UUID16                     0x2902

/** GATT Client Presentation Format descriptor 16-bit UUID. */
#define BLE_GATT_DSC_CLT_PRE_FMT16                      0x2904

/** GATT Client Aggregate Format descriptor 16-bit UUID. */
#define BLE_GATT_DSC_CLT_AGG_FMT16                      0x2905

/** @} */

/**
 * @defgroup ble_gatt_chr_properties Generic Attribute Profile (GATT) Characteristic Properties
 * @{
 */

/** Characteristic property: Broadcast. */
#define BLE_GATT_CHR_PROP_BROADCAST                     0x01

/** Characteristic property: Read. */
#define BLE_GATT_CHR_PROP_READ                          0x02

/** Characteristic property: Write Without Response. */
#define BLE_GATT_CHR_PROP_WRITE_NO_RSP                  0x04

/** Characteristic property: Write. */
#define BLE_GATT_CHR_PROP_WRITE                         0x08

/** Characteristic property: Notify. */
#define BLE_GATT_CHR_PROP_NOTIFY                        0x10

/** Characteristic property: Indicate. */
#define BLE_GATT_CHR_PROP_INDICATE                      0x20

/** Characteristic property: Authenticated Signed Write. */
#define BLE_GATT_CHR_PROP_AUTH_SIGN_WRITE               0x40

/** Characteristic property: Extended Properties. */
#define BLE_GATT_CHR_PROP_EXTENDED                      0x80

/** @} */

/** @defgroup ble_gatt_access_op_codes Generic Attribute Profile (GATT) Access Operation Codes
 * @{
 */

/** GATT attribute access operation: Read characteristic. */
#define BLE_GATT_ACCESS_OP_READ_CHR                     0

/** GATT attribute access operation: Write characteristic. */
#define BLE_GATT_ACCESS_OP_WRITE_CHR                    1

/** GATT attribute access operation: Read descriptor. */
#define BLE_GATT_ACCESS_OP_READ_DSC                     2

/** GATT attribute access operation: Write descriptor. */
#define BLE_GATT_ACCESS_OP_WRITE_DSC                    3

/** @} */

/**
 * @defgroup ble_gatt_chr_flags Generic Attribute Profile (GATT) Characteristic Flags
 * @{
 */

/** GATT Characteristic Flag: Broadcast property. */
#define BLE_GATT_CHR_F_BROADCAST                        0x0001

/** GATT Characteristic Flag: Read property. */
#define BLE_GATT_CHR_F_READ                             0x0002

/** GATT Characteristic Flag: Write without Response property. */
#define BLE_GATT_CHR_F_WRITE_NO_RSP                     0x0004

/** GATT Characteristic Flag: Write property. */
#define BLE_GATT_CHR_F_WRITE                            0x0008

/** GATT Characteristic Flag: Notify property. */
#define BLE_GATT_CHR_F_NOTIFY                           0x0010

/** GATT Characteristic Flag: Indicate property. */
#define BLE_GATT_CHR_F_INDICATE                         0x0020

/** GATT Characteristic Flag: Authenticated Signed Writes property. */
#define BLE_GATT_CHR_F_AUTH_SIGN_WRITE                  0x0040

/** GATT Characteristic Flag: Reliable Writes property. */
#define BLE_GATT_CHR_F_RELIABLE_WRITE                   0x0080

/** GATT Characteristic Flag: Auxiliary Writes permission. */
#define BLE_GATT_CHR_F_AUX_WRITE                        0x0100

/** GATT Characteristic Flag: Read Encrypted permission. */
#define BLE_GATT_CHR_F_READ_ENC                         0x0200

/** GATT Characteristic Flag: Read Authenticated permission. */
#define BLE_GATT_CHR_F_READ_AUTHEN                      0x0400

/** GATT Characteristic Flag: Read Authorized permission. */
#define BLE_GATT_CHR_F_READ_AUTHOR                      0x0800

/** GATT Characteristic Flag: Write Encrypted permission. */
#define BLE_GATT_CHR_F_WRITE_ENC                        0x1000

/** GATT Characteristic Flag: Write Authenticated permission. */
#define BLE_GATT_CHR_F_WRITE_AUTHEN                     0x2000

/** GATT Characteristic Flag: Write Authorized permission. */
#define BLE_GATT_CHR_F_WRITE_AUTHOR                     0x4000


/** @} */

/**
 * @defgroup ble_gatt_service_types Generic Attribute Profile (GATT) Service Types
 * @{
 */

/** GATT Service Type: End of Services. */
#define BLE_GATT_SVC_TYPE_END                           0

/** GATT Service Type: Primary Service. */
#define BLE_GATT_SVC_TYPE_PRIMARY                       1

/** GATT Service Type: Secondary Service. */
#define BLE_GATT_SVC_TYPE_SECONDARY                     2

/** @} */

/**
 * @defgroup Client Presentation Format - GATT Format Types
 * @{
 */

/** GATT Format Type: unsigned 1-bit; 0 = false; 1 = true */
#define BLE_GATT_CHR_FORMAT_BOOLEAN                     0x01

/** GATT Format Type: unsigned 2-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT2                       0x02

/** GATT Format Type: unsigned 4-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT4                       0x03

/** GATT Format Type: unsigned 8-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT8                       0x04

/** GATT Format Type: unsigned 12-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT12                      0x05

/** GATT Format Type: unsigned 16-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT16                      0x06

/** GATT Format Type: unsigned 24-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT24                      0x07

/** GATT Format Type: unsigned 32-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT32                      0x08

/** GATT Format Type: unsigned 48-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT48                      0x09

/** GATT Format Type: unsigned 64-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT64                      0x0A

/** GATT Format Type: unsigned 128-bit integer */
#define BLE_GATT_CHR_FORMAT_UINT128                     0x0B

/** GATT Format Type: signed 8-bit integer */
#define BLE_GATT_CHR_FORMAT_SINT8                       0x0C

/** GATT Format Type: signed 12-bit integer */
#define BLE_GATT_CHR_FORMAT_SINT12                      0x0D

/** GATT Format Type: signed 16-bit integer */
#define BLE_GATT_CHR_FORMAT_SINT16                      0x0E

/** GATT Format Type: signed 24-bit integer */
#define BLE_GATT_CHR_FORMAT_SINT24                      0x0F

/** GATT Format Type: signed 32-bit integer */
#define BLE_GATT_CHR_FORMAT_SINT32                      0x10

/** GATT Format Type: signed 48-bit integer */
#define BLE_GATT_CHR_FORMAT_SINT48                      0x11

/** GATT Format Type: signed 64-bit integer */
#define BLE_GATT_CHR_FORMAT_SINT64                      0x12

/** GATT Format Type: signed 128-bit integer */
#define BLE_GATT_CHR_FORMAT_SINT128                     0x13

/** GATT Format Type: IEEE-754 32-bit floating point */
#define BLE_GATT_CHR_FORMAT_FLOAT32                     0x14

/** GATT Format Type: IEEE-754 64-bit floating point */
#define BLE_GATT_CHR_FORMAT_FLOAT64                     0x15

/** GATT Format Type: IEEE 11073-20601 16-bit SFLOAT */
#define BLE_GATT_CHR_FORMAT_MEDFLOAT16                  0x16

/** GATT Format Type: IEEE 11073-20601 32-bit FLOAT */
#define BLE_GATT_CHR_FORMAT_MEDFLOAT32                  0x17

/** GATT Format Type: IEEE 11073-20601 nomenclature code (uint16[2]) */
#define BLE_GATT_CHR_FORMAT_UINT16_2                    0x18

/** GATT Format Type: UTF-8 string */
#define BLE_GATT_CHR_FORMAT_UTF8S                       0x19

/** GATT Format Type: UTF-16 string */
#define BLE_GATT_CHR_FORMAT_UTF16S                      0x1A

/** GATT Format Type: opaque structure */
#define BLE_GATT_CHR_FORMAT_STRUCT                      0x1B

/** GATT Format Type: IEEE-11073 ASN.1/MDER structure */
#define BLE_GATT_CHR_FORMAT_MEDASN1                     0x1C

/** @} */

/**
 * @defgroup Client Presentation Format - GATT Unit UUIDs
 * @{
 */

/** GATT Unit UUID: Unitless */
#define BLE_GATT_CHR_UNIT_UNITLESS                              0x2700

/** GATT Unit UUID: Length */
#define BLE_GATT_CHR_UNIT_METRE                                 0x2701

/** GATT Unit UUID: Mass */
#define BLE_GATT_CHR_UNIT_KILOGRAM                              0x2702

/** GATT Unit UUID: Time */
#define BLE_GATT_CHR_UNIT_SECOND                                0x2703

/** GATT Unit UUID: Electric Current */
#define BLE_GATT_CHR_UNIT_AMPERE                                0x2704

/** GATT Unit UUID: Thermodynamic Temperature */
#define BLE_GATT_CHR_UNIT_KELVIN                                0x2705

/** GATT Unit UUID: Amount Of Substance */
#define BLE_GATT_CHR_UNIT_MOLE                                  0x2706

/** GATT Unit UUID: Luminous Intensity */
#define BLE_GATT_CHR_UNIT_CANDELA                               0x2707

/** GATT Unit UUID: Area */
#define BLE_GATT_CHR_UNIT_SQUARE_METRES                         0x2710

/** GATT Unit UUID: Volume */
#define BLE_GATT_CHR_UNIT_CUBIC_METRES                          0x2711

/** GATT Unit UUID: Velocity */
#define BLE_GATT_CHR_UNIT_METRES_PER_SECOND                     0x2712

/** GATT Unit UUID: Acceleration */
#define BLE_GATT_CHR_UNIT_METRES_PER_SECOND_SQUARED             0x2713

/** GATT Unit UUID: Wavenumber */
#define BLE_GATT_CHR_UNIT_RECIPROCAL_METRE                      0x2714

/** GATT Unit UUID: Density */
#define BLE_GATT_CHR_UNIT_KILOGRAM_PER_CUBIC_METRE_DENSITY      0x2715

/** GATT Unit UUID: Surface Density */
#define BLE_GATT_CHR_UNIT_KILOGRAM_PER_SQUARE_METRE             0x2716

/** GATT Unit UUID: Specific Volume */
#define BLE_GATT_CHR_UNIT_CUBIC_METRE_PER_KILOGRAM              0x2717

/** GATT Unit UUID: Current Density */
#define BLE_GATT_CHR_UNIT_AMPERE_PER_SQUARE_METRE               0x2718

/** GATT Unit UUID: Magnetic Field Strength */
#define BLE_GATT_CHR_UNIT_AMPERE_PER_METRE                      0x2719

/** GATT Unit UUID: Amount Concentration */
#define BLE_GATT_CHR_UNIT_MOLE_PER_CUBIC_METRE                  0x271A

/** GATT Unit UUID: Mass Concentration */
#define BLE_GATT_CHR_UNIT_KILOGRAM_PER_CUBIC_METRE_MASS_CONC    0x271B

/** GATT Unit UUID: Luminance */
#define BLE_GATT_CHR_UNIT_CANDELA_PER_SQUARE_METRE              0x271C

/** GATT Unit UUID: Refractive Index */
#define BLE_GATT_CHR_UNIT_REFRACTIVE_INDEX                      0x271D

/** GATT Unit UUID: Relative Permeability */
#define BLE_GATT_CHR_UNIT_RELATIVE_PERMEABILITY                 0x271E

/** GATT Unit UUID: Plane Angle */
#define BLE_GATT_CHR_UNIT_RADIAN                                0x2720

/** GATT Unit UUID: Solid Angle */
#define BLE_GATT_CHR_UNIT_STERADIAN                             0x2721

/** GATT Unit UUID: Frequency */
#define BLE_GATT_CHR_UNIT_HERTZ                                 0x2722

/** GATT Unit UUID: Force */
#define BLE_GATT_CHR_UNIT_NEWTON                                0x2723

/** GATT Unit UUID: Pressure */
#define BLE_GATT_CHR_UNIT_PASCAL                                0x2724

/** GATT Unit UUID: Energy */
#define BLE_GATT_CHR_UNIT_JOULE                                 0x2725

/** GATT Unit UUID: Power */
#define BLE_GATT_CHR_UNIT_WATT                                  0x2726

/** GATT Unit UUID: Electric Charge */
#define BLE_GATT_CHR_UNIT_COULOMB                               0x2727

/** GATT Unit UUID: Electric Potential Difference */
#define BLE_GATT_CHR_UNIT_VOLT                                  0x2728

/** GATT Unit UUID: Capacitance */
#define BLE_GATT_CHR_UNIT_FARAD                                 0x2729

/** GATT Unit UUID: Electric Resistance */
#define BLE_GATT_CHR_UNIT_OHM                                   0x272A

/** GATT Unit UUID: Electric Conductance */
#define BLE_GATT_CHR_UNIT_SIEMENS                               0x272B

/** GATT Unit UUID: Magnetic Flux */
#define BLE_GATT_CHR_UNIT_WEBER                                 0x272C

/** GATT Unit UUID: Magnetic Flux Density */
#define BLE_GATT_CHR_UNIT_TESLA                                 0x272D

/** GATT Unit UUID: Inductance */
#define BLE_GATT_CHR_UNIT_HENRY                                 0x272E

/** GATT Unit UUID: celsius Temperature */
#define BLE_GATT_CHR_UNIT_DEGREE_CELSIUS                        0x272F

/** GATT Unit UUID: Luminous Flux */
#define BLE_GATT_CHR_UNIT_LUMEN                                 0x2730

/** GATT Unit UUID: Illuminance */
#define BLE_GATT_CHR_UNIT_LUX                                   0x2731

/** GATT Unit UUID: Activity Referred To A Radionuclide */
#define BLE_GATT_CHR_UNIT_BECQUEREL                             0x2732

/** GATT Unit UUID: Absorbed Dose */
#define BLE_GATT_CHR_UNIT_GRAY                                  0x2733

/** GATT Unit UUID: Dose Equivalent */
#define BLE_GATT_CHR_UNIT_SIEVERT                               0x2734

/** GATT Unit UUID: Catalytic Activity */
#define BLE_GATT_CHR_UNIT_KATAL                                 0x2735

/** GATT Unit UUID: Dynamic Viscosity */
#define BLE_GATT_CHR_UNIT_PASCAL_SECOND                         0x2740

/** GATT Unit UUID: Moment Of Force */
#define BLE_GATT_CHR_UNIT_NEWTON_METRE                          0x2741

/** GATT Unit UUID: Surface Tension */
#define BLE_GATT_CHR_UNIT_NEWTON_PER_METRE                      0x2742

/** GATT Unit UUID: Angular Velocity */
#define BLE_GATT_CHR_UNIT_RADIAN_PER_SECOND                     0x2743

/** GATT Unit UUID: Angular Acceleration */
#define BLE_GATT_CHR_UNIT_RADIAN_PER_SECOND_SQUARED             0x2744

/** GATT Unit UUID: Heat Flux Density */
#define BLE_GATT_CHR_UNIT_WATT_PER_SQUARE_METRE_HEAT            0x2745

/** GATT Unit UUID: Heat Capacity */
#define BLE_GATT_CHR_UNIT_JOULE_PER_KELVIN                      0x2746

/** GATT Unit UUID: Specific Heat Capacity */
#define BLE_GATT_CHR_UNIT_JOULE_PER_KILOGRAM_KELVIN             0x2747

/** GATT Unit UUID: Specific Energy */
#define BLE_GATT_CHR_UNIT_JOULE_PER_KILOGRAM                    0x2748

/** GATT Unit UUID: Thermal Conductivity */
#define BLE_GATT_CHR_UNIT_WATT_PER_METRE_KELVIN                 0x2749

/** GATT Unit UUID: Energy Density */
#define BLE_GATT_CHR_UNIT_JOULE_PER_CUBIC_METRE                 0x274A

/** GATT Unit UUID: Electric Field Strength */
#define BLE_GATT_CHR_UNIT_VOLT_PER_METRE                        0x274B

/** GATT Unit UUID: Electric Charge Density */
#define BLE_GATT_CHR_UNIT_COULOMB_PER_CUBIC_METRE               0x274C

/** GATT Unit UUID: Surface Charge Density */
#define BLE_GATT_CHR_UNIT_COULOMB_PER_SQUARE_METRE_CHARGE       0x274D

/** GATT Unit UUID: Electric Flux Density */
#define BLE_GATT_CHR_UNIT_COULOMB_PER_SQUARE_METRE_FLUX         0x274E

/** GATT Unit UUID: Permittivity */
#define BLE_GATT_CHR_UNIT_FARAD_PER_METRE                       0x274F

/** GATT Unit UUID: Permeability */
#define BLE_GATT_CHR_UNIT_HENRY_PER_METRE                       0x2750

/** GATT Unit UUID: Molar Energy */
#define BLE_GATT_CHR_UNIT_JOULE_PER_MOLE                        0x2751

/** GATT Unit UUID: Molar Entropy */
#define BLE_GATT_CHR_UNIT_JOULE_PER_MOLE_KELVIN                 0x2752

/** GATT Unit UUID: Exposure */
#define BLE_GATT_CHR_UNIT_COULOMB_PER_KILOGRAM                  0x2753

/** GATT Unit UUID: Absorbed Dose Rate */
#define BLE_GATT_CHR_UNIT_GRAY_PER_SECOND                       0x2754

/** GATT Unit UUID: Radiant Intensity */
#define BLE_GATT_CHR_UNIT_WATT_PER_STERADIAN                    0x2755

/** GATT Unit UUID: Radiance */
#define BLE_GATT_CHR_UNIT_WATT_PER_SQUARE_METRE_STERADIAN       0x2756

/** GATT Unit UUID: Catalytic Activity Concentration */
#define BLE_GATT_CHR_UNIT_KATAL_PER_CUBIC_METRE                 0x2757

/** GATT Unit UUID: Time */
#define BLE_GATT_CHR_UNIT_MINUTE                                0x2760

/** GATT Unit UUID: Time */
#define BLE_GATT_CHR_UNIT_HOUR                                  0x2761

/** GATT Unit UUID: Time */
#define BLE_GATT_CHR_UNIT_DAY                                   0x2762

/** GATT Unit UUID: Plane Angle */
#define BLE_GATT_CHR_UNIT_DEGREE                                0x2763

/** GATT Unit UUID: Plane Angle */
#define BLE_GATT_CHR_UNIT_MINUTE_ANGLE                          0x2764

/** GATT Unit UUID: Plane Angle */
#define BLE_GATT_CHR_UNIT_SECOND_ANGLE                          0x2765

/** GATT Unit UUID: Area */
#define BLE_GATT_CHR_UNIT_HECTARE                               0x2766

/** GATT Unit UUID: Volume */
#define BLE_GATT_CHR_UNIT_LITRE                                 0x2767

/** GATT Unit UUID: Mass */
#define BLE_GATT_CHR_UNIT_TONNE                                 0x2768

/** GATT Unit UUID: Pressure */
#define BLE_GATT_CHR_UNIT_BAR                                   0x2780

/** GATT Unit UUID: Pressure */
#define BLE_GATT_CHR_UNIT_MILLIMETRE_OF_MERCURY                 0x2781

/** GATT Unit UUID: Length */
#define BLE_GATT_CHR_UNIT_ANGSTROM                              0x2782

/** GATT Unit UUID: Length */
#define BLE_GATT_CHR_UNIT_NAUTICAL_MILE                         0x2783

/** GATT Unit UUID: Area */
#define BLE_GATT_CHR_UNIT_BARN                                  0x2784

/** GATT Unit UUID: Velocity */
#define BLE_GATT_CHR_UNIT_KNOT                                  0x2785

/** GATT Unit UUID: Logarithmic Radio Quantity */
#define BLE_GATT_CHR_UNIT_NEPER                                 0x2786

/** GATT Unit UUID: Logarithmic Radio Quantity */
#define BLE_GATT_CHR_UNIT_BEL                                   0x2787

/** GATT Unit UUID: Length */
#define BLE_GATT_CHR_UNIT_YARD                                  0x27A0

/** GATT Unit UUID: Length */
#define BLE_GATT_CHR_UNIT_PARSEC                                0x27A1

/** GATT Unit UUID: Length */
#define BLE_GATT_CHR_UNIT_INCH                                  0x27A2

/** GATT Unit UUID: Length */
#define BLE_GATT_CHR_UNIT_FOOT                                  0x27A3

/** GATT Unit UUID: Length */
#define BLE_GATT_CHR_UNIT_MILE                                  0x27A4

/** GATT Unit UUID: Pressure */
#define BLE_GATT_CHR_UNIT_POUND_FORCE_PER_SQUARE_INCH           0x27A5

/** GATT Unit UUID: Velocity */
#define BLE_GATT_CHR_UNIT_KILOMETRE_PER_HOUR                    0x27A6

/** GATT Unit UUID: Velocity */
#define BLE_GATT_CHR_UNIT_MILE_PER_HOUR                         0x27A7

/** GATT Unit UUID: Angular Velocity */
#define BLE_GATT_CHR_UNIT_REVOLUTION_PER_MINUTE                 0x27A8

/** GATT Unit UUID: Energy */
#define BLE_GATT_CHR_UNIT_GRAM_CALORIE                          0x27A9

/** GATT Unit UUID: Energy */
#define BLE_GATT_CHR_UNIT_KILOGRAM_CALORIE                      0x27AA

/** GATT Unit UUID: Energy */
#define BLE_GATT_CHR_UNIT_KILOWATT_HOUR                         0x27AB

/** GATT Unit UUID: Thermodynamic Temperature */
#define BLE_GATT_CHR_UNIT_DEGREE_FAHRENHEIT                     0x27AC

/** GATT Unit UUID: Percentage */
#define BLE_GATT_CHR_UNIT_PERCENTAGE                            0x27AD

/** GATT Unit UUID: Per Mille */
#define BLE_GATT_CHR_UNIT_PER_MILLE                             0x27AE

/** GATT Unit UUID: Period */
#define BLE_GATT_CHR_UNIT_BEATS_PER_MINUTE                      0x27AF

/** GATT Unit UUID: Electric Charge */
#define BLE_GATT_CHR_UNIT_AMPERE_HOURS                          0x27B0

/** GATT Unit UUID: Mass Density */
#define BLE_GATT_CHR_UNIT_MILLIGRAM_PER_DECILITRE               0x27B1

/** GATT Unit UUID: Mass Density */
#define BLE_GATT_CHR_UNIT_MILLIMOLE_PER_LITRE                   0x27B2

/** GATT Unit UUID: Time */
#define BLE_GATT_CHR_UNIT_YEAR                                  0x27B3

/** GATT Unit UUID: Time */
#define BLE_GATT_CHR_UNIT_MONTH                                 0x27B4

/** GATT Unit UUID: Concentration */
#define BLE_GATT_CHR_UNIT_COUNT_PER_CUBIC_METRE                 0x27B5

/** GATT Unit UUID: Irradiance */
#define BLE_GATT_CHR_UNIT_WATT_PER_SQUARE_METRE_IRRADIANCE      0x27B6

/** GATT Unit UUID: Milliliter */
#define BLE_GATT_CHR_UNIT_PER_KILOGRAM_PER_MINUTE               0x27B7

/** GATT Unit UUID: Mass */
#define BLE_GATT_CHR_UNIT_POUND                                 0x27B8

/** GATT Unit UUID: Metabolic Equivalent */
#define BLE_GATT_CHR_UNIT_METABOLIC_EQUIVALENT                  0x27B9

/** GATT Unit UUID: Step */
#define BLE_GATT_CHR_UNIT_PER_MINUTE_STEP                       0x27BA

/** GATT Unit UUID: Stroke */
#define BLE_GATT_CHR_UNIT_PER_MINUTE_STROKE                     0x27BC

/** GATT Unit UUID: Pace */
#define BLE_GATT_CHR_UNIT_KILOMETRE_PER_MINUTE                  0x27BD

/** GATT Unit UUID: Luminous Efficacy */
#define BLE_GATT_CHR_UNIT_LUMEN_PER_WATT                        0x27BE

/** GATT Unit UUID: Luminous Energy */
#define BLE_GATT_CHR_UNIT_LUMEN_HOUR                            0x27BF

/** GATT Unit UUID: Luminous Exposure */
#define BLE_GATT_CHR_UNIT_LUX_HOUR                              0x27C0

/** GATT Unit UUID: Mass Flow */
#define BLE_GATT_CHR_UNIT_GRAM_PER_SECOND                       0x27C1

/** GATT Unit UUID: Volume Flow */
#define BLE_GATT_CHR_UNIT_LITRE_PER_SECOND                      0x27C2

/** GATT Unit UUID: Sound Pressure */
#define BLE_GATT_CHR_UNIT_DECIBEL                               0x27C3

/** GATT Unit UUID: Concentration */
#define BLE_GATT_CHR_UNIT_PARTS_PER_MILLION                     0x27C4

/** GATT Unit UUID: Concentration */
#define BLE_GATT_CHR_UNIT_PARTS_PER_BILLION                     0x27C5

/** GATT Unit UUID: Mass Density Rate */
#define BLE_GATT_CHR_UNIT_MILLIGRAM_PER_DECILITRE_PER_MINUTE    0x27C6

/** GATT Unit UUID: Electrical Apparent Energy */
#define BLE_GATT_CHR_UNIT_KILOVOLT_AMPERE_HOUR                  0x27C7

/** GATT Unit UUID: Electrical Apparent Power */
#define BLE_GATT_CHR_UNIT_VOLT_AMPERE                           0x27C8

/** @} */

/**
 * @defgroup Client Presentation Format - GATT Name Space
 * @{
 */

/** GATT Name Space: Bluetooth SIG */
#define BLE_GATT_CHR_NAMESPACE_BT_SIG                   0x01

/** @} */

/**
 * @defgroup Client Presentation Format - GATT Description for name space Bluetooth SIG
 * @{
 */

/** GATT Description: Unknown */
#define BLE_GATT_CHR_BT_SIG_DESC_UNKNOWN                0x0000

/**
 * 0x0001 - 0x00FF represent themselves.
 * See Assigned Numbers 2.4.2.1 for more info
 */

/** GATT Description: front */
#define BLE_GATT_CHR_BT_SIG_DESC_FRONT                  0x0100

/** GATT Description: back */
#define BLE_GATT_CHR_BT_SIG_DESC_BACK                   0x0101

/** GATT Description: top */
#define BLE_GATT_CHR_BT_SIG_DESC_TOP                    0x0102

/** GATT Description: bottom */
#define BLE_GATT_CHR_BT_SIG_DESC_BOTTOM                 0x0103

/** GATT Description: upper */
#define BLE_GATT_CHR_BT_SIG_DESC_UPPER                  0x0104

/** GATT Description: lower */
#define BLE_GATT_CHR_BT_SIG_DESC_LOWER                  0x0105

/** GATT Description: main */
#define BLE_GATT_CHR_BT_SIG_DESC_MAIN                   0x0106

/** GATT Description: backup */
#define BLE_GATT_CHR_BT_SIG_DESC_BACKUP                 0x0107

/** GATT Description: auxiliary */
#define BLE_GATT_CHR_BT_SIG_DESC_AUXILIARY              0x0108

/** GATT Description: supplementary */
#define BLE_GATT_CHR_BT_SIG_DESC_SUPPLEMENTARY          0x0109

/** GATT Description: flash */
#define BLE_GATT_CHR_BT_SIG_DESC_FLASH                  0x010A

/** GATT Description: inside */
#define BLE_GATT_CHR_BT_SIG_DESC_INSIDE                 0x010B

/** GATT Description: outside */
#define BLE_GATT_CHR_BT_SIG_DESC_OUTSIDE                0x010C

/** GATT Description: left */
#define BLE_GATT_CHR_BT_SIG_DESC_LEFT                   0x010D

/** GATT Description: right */
#define BLE_GATT_CHR_BT_SIG_DESC_RIGHT                  0x010E

/** GATT Description: internal */
#define BLE_GATT_CHR_BT_SIG_DESC_INTERNAL               0x010F

/** GATT Description: external */
#define BLE_GATT_CHR_BT_SIG_DESC_EXTERNAL               0x0110

/** @} */

/*** @client. */
/** Represents a GATT error. */
struct ble_gatt_error {
    /** The GATT status code indicating the type of error. */
    uint16_t status;

    /** The attribute handle associated with the error. */
    uint16_t att_handle;
};

/** Represents a GATT Service. */
struct ble_gatt_svc {
    /** The start handle of the GATT service. */
    uint16_t start_handle;

    /** The end handle of the GATT service. */
    uint16_t end_handle;

    /** The UUID of the GATT service. */
    ble_uuid_any_t uuid;
};


/** Represents a GATT attribute. */
struct ble_gatt_attr {
    /** The handle of the GATT attribute. */
    uint16_t handle;

    /** The offset of the data within the attribute. */
    uint16_t offset;

    /** Pointer to the data buffer represented by an os_mbuf. */
    struct os_mbuf *om;
};


/** Represents a GATT characteristic. */
struct ble_gatt_chr {
    /** The handle of the GATT characteristic definition. */
    uint16_t def_handle;

    /** The handle of the GATT characteristic value. */
    uint16_t val_handle;

    /** The properties of the GATT characteristic. */
    uint8_t properties;

    /** The UUID of the GATT characteristic. */
    ble_uuid_any_t uuid;
};


/** Represents a GATT descriptor. */
struct ble_gatt_dsc {
    /** The handle of the GATT descriptor. */
    uint16_t handle;

    /** The UUID of the GATT descriptor. */
    ble_uuid_any_t uuid;
};


/** Represents a handle-value tuple for multiple handle notifications. */
struct ble_gatt_notif {
    /** The handle of the GATT characteristic */
    uint16_t handle;

    /** The buffer with GATT characteristic value */
    struct os_mbuf *value;
};

/** Function prototype for the GATT MTU exchange callback. */
typedef int ble_gatt_mtu_fn(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            uint16_t mtu, void *arg);

/** Function prototype for the GATT service discovery callback. */
typedef int ble_gatt_disc_svc_fn(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_svc *service,
                                 void *arg);

/**
 * The host will free the attribute mbuf automatically after the callback is
 * executed.  The application can take ownership of the mbuf and prevent it
 * from being freed by assigning NULL to attr->om.
 */
typedef int ble_gatt_attr_fn(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr,
                             void *arg);

/**
 * The host will free the attribute mbuf automatically after the callback is
 * executed.  The application can take ownership of the mbuf and prevent it
 * from being freed by assigning NULL to attr->om.
 */
typedef int ble_gatt_attr_mult_fn(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attrs,
                                  uint8_t num_attrs,
                                  void *arg);

/**
 * The host will free the attribute mbufs automatically after the callback is
 * executed.  The application can take ownership of the mbufs and prevent them
 * from being freed by assigning NULL to each attribute's om field.
 */
typedef int ble_gatt_reliable_attr_fn(uint16_t conn_handle,
                                      const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attrs,
                                      uint8_t num_attrs, void *arg);

/** Function prototype for the GATT characteristic callback. */
typedef int ble_gatt_chr_fn(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            const struct ble_gatt_chr *chr, void *arg);

/** Function prototype for the GATT descriptor callback. */
typedef int ble_gatt_dsc_fn(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            uint16_t chr_val_handle,
                            const struct ble_gatt_dsc *dsc,
                            void *arg);

/**
 * Initiates GATT procedure: Exchange MTU.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_exchange_mtu(uint16_t conn_handle,
                           ble_gatt_mtu_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Discover All Primary Services.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_disc_all_svcs(uint16_t conn_handle,
                            ble_gatt_disc_svc_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Discover Primary Service by Service UUID.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param uuid                  The 128-bit UUID of the service to discover.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_disc_svc_by_uuid(uint16_t conn_handle, const ble_uuid_t *uuid,
                               ble_gatt_disc_svc_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Find Included Services.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle to begin the search at (generally
 *                                  the service definition handle).
 * @param end_handle            The handle to end the search at (generally the
 *                                  last handle in the service).
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_find_inc_svcs(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle,
                            ble_gatt_disc_svc_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Discover All Characteristics of a Service.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle to begin the search at (generally
 *                                  the service definition handle).
 * @param end_handle            The handle to end the search at (generally the
 *                                  last handle in the service).
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_disc_all_chrs(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle, ble_gatt_chr_fn *cb,
                            void *cb_arg);

/**
 * Initiates GATT procedure: Discover Characteristics by UUID.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle to begin the search at (generally
 *                                  the service definition handle).
 * @param end_handle            The handle to end the search at (generally the
 *                                  last handle in the service).
 * @param uuid                  The 128-bit UUID of the characteristic to
 *                                  discover.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_disc_chrs_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                               uint16_t end_handle, const ble_uuid_t *uuid,
                               ble_gatt_chr_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Discover All Characteristic Descriptors.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The handle of the characteristic value
 *                                  attribute.
 * @param end_handle            The last handle in the characteristic
 *                                  definition.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_disc_all_dscs(uint16_t conn_handle, uint16_t start_handle,
                            uint16_t end_handle,
                            ble_gatt_dsc_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Read Characteristic Value.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to read.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_read(uint16_t conn_handle, uint16_t attr_handle,
                   ble_gatt_attr_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Read Using Characteristic UUID.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param start_handle          The first handle to search (generally the
 *                                  handle of the service definition).
 * @param end_handle            The last handle to search (generally the
 *                                  last handle in the service definition).
 * @param uuid                  The 128-bit UUID of the characteristic to
 *                                  read.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_read_by_uuid(uint16_t conn_handle, uint16_t start_handle,
                           uint16_t end_handle, const ble_uuid_t *uuid,
                           ble_gatt_attr_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Read Long Characteristic Values.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param handle                The handle of the characteristic value to read.
 * @param offset                The offset within the characteristic value to
 *                                  start reading.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_read_long(uint16_t conn_handle, uint16_t handle, uint16_t offset,
                        ble_gatt_attr_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Read Multiple Characteristic Values.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param handles               An array of 16-bit attribute handles to read.
 * @param num_handles           The number of entries in the "handles" array.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_read_mult(uint16_t conn_handle, const uint16_t *handles,
                        uint8_t num_handles, ble_gatt_attr_fn *cb,
                        void *cb_arg);

/**
 * Initiates GATT procedure: Read Multiple Variable Length Characteristic Values.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param handles               An array of 16-bit attribute handles to read.
 * @param num_handles           The number of entries in the "handles" array.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_read_mult_var(uint16_t conn_handle, const uint16_t *handles,
                            uint8_t num_handles, ble_gatt_attr_mult_fn *cb,
                            void *cb_arg);
/**
 * Initiates GATT procedure: Write Without Response.  This function consumes
 * the supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param om                    The value to write to the characteristic.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_write_no_rsp(uint16_t conn_handle, uint16_t attr_handle,
                           struct os_mbuf *om);

/**
 * Initiates GATT procedure: Write Without Response.  This function consumes
 * the supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param data                  The value to write to the characteristic.
 * @param data_len              The number of bytes to write.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_write_no_rsp_flat(uint16_t conn_handle, uint16_t attr_handle,
                                const void *data, uint16_t data_len);

/**
 * Initiates GATT procedure: Write Characteristic Value.  This function
 * consumes the supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param om                    The value to write to the characteristic.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_write(uint16_t conn_handle, uint16_t attr_handle,
                    struct os_mbuf *om,
                    ble_gatt_attr_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Write Characteristic Value (flat buffer version).
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param data                  The value to write to the characteristic.
 * @param data_len              The number of bytes to write.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_write_flat(uint16_t conn_handle, uint16_t attr_handle,
                         const void *data, uint16_t data_len,
                         ble_gatt_attr_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Write Long Characteristic Values.  This function
 * consumes the supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attr_handle           The handle of the characteristic value to write
 *                                  to.
 * @param offset                The offset at which to begin writing the value.
 * @param om                    The value to write to the characteristic.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_write_long(uint16_t conn_handle, uint16_t attr_handle,
                         uint16_t offset, struct os_mbuf *om,
                         ble_gatt_attr_fn *cb, void *cb_arg);

/**
 * Initiates GATT procedure: Reliable Writes.  This function consumes the
 * supplied mbufs regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param attrs                 An array of attribute descriptors; specifies
 *                                  which characteristics to write to and what
 *                                  data to write to them.  The mbuf pointer in
 *                                  each attribute is set to NULL by this
 *                                  function.
 * @param num_attrs             The number of characteristics to write; equal
 *                                  to the number of elements in the 'attrs'
 *                                  array.
 * @param cb                    The function to call to report procedure status
 *                                  updates; null for no callback.
 * @param cb_arg                The optional argument to pass to the callback
 *                                  function.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_write_reliable(uint16_t conn_handle,
                             struct ble_gatt_attr *attrs,
                             int num_attrs, ble_gatt_reliable_attr_fn *cb,
                             void *cb_arg);

/**
 * Sends a "free-form" characteristic notification.  This function consumes the
 * supplied mbuf regardless of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param att_handle            The attribute handle to indicate in the
 *                                  outgoing notification.
 * @param om                    The value to write to the characteristic.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gatts_notify_custom(uint16_t conn_handle, uint16_t att_handle,
                            struct os_mbuf *om);

/**
 * Sends a "free-form" multiple handle variable length characteristic
 * notification. This function consumes supplied mbufs regardless of the
 * outcome. Notifications are sent in order of supplied entries.
 * Function tries to send minimum amount of PDUs. If PDU can't contain all
 * of the characteristic values, multiple notifications are sent. If only one
 * handle-value pair fits into PDU, or only one characteristic remains in the
 * list, regular characteristic notification is sent.
 *
 * If GATT client doesn't support receiving multiple handle notifications,
 * this will use GATT notification for each characteristic, separately.
 *
 * If value of characteristic is not specified it will be read from local
 * GATT database.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_count             Number of characteristics to notify about.
 * @param tuples                Handle-value pairs in form of `ble_gatt_notif`
 *                                  structures.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gatts_notify_multiple_custom(uint16_t conn_handle,
                                     size_t chr_count,
                                     struct ble_gatt_notif *tuples);

/**
 * @deprecated Should not be used. Use ble_gatts_notify_custom instead.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param att_handle            The attribute handle to indicate in the
 *                                  outgoing notification.
 * @param om                    The value to write to the characteristic.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_notify_custom(uint16_t conn_handle, uint16_t att_handle,
                            struct os_mbuf *om);

/**
 * Sends a characteristic notification.  The content of the message is read
 * from the specified characteristic.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The value attribute handle of the
 *                                  characteristic to include in the outgoing
 *                                  notification.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gatts_notify(uint16_t conn_handle, uint16_t chr_val_handle);

/**
 * @deprecated Should not be used. Use ble_gatts_notify instead.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The value attribute handle of the
 *                                  characteristic to include in the outgoing
 *                                  notification.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_notify(uint16_t conn_handle, uint16_t chr_val_handle);

/**
 * Sends a multiple handle variable length characteristic notification.  The
 * content of the message is read from the specified characteristics.
 * Notifications are sent in order of supplied handles. Function tries to
 * send minimum amount of PDUs. If PDU can't contain all of the
 * characteristic values, multiple notifications are sent. If only one
 * handle-value pair fits into PDU, or only one characteristic remains in the
 * list, regular characteristic notification is sent.
 *
 * If GATT client doesn't support receiving multiple handle notifications,
 * this will use GATT notification for each characteristic, separately.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param num_handles           The number of entries in the "chr_val_handles"
 *                                  array.
 * @param chr_val_handles       Array of attribute handles of the
 *                                  characteristics to include in the outgoing
 *                                  notification.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gatts_notify_multiple(uint16_t conn_handle,
                              size_t num_handles,
                              const uint16_t *chr_val_handles);

/**
 * Sends a "free-form" characteristic indication.  The provided mbuf contains
 * the indication payload.  This function consumes the supplied mbuf regardless
 * of the outcome.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The value attribute handle of the
 *                                  characteristic to include in the outgoing
 *                                  indication.
 * @param txom                  The data to include in the indication.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gatts_indicate_custom(uint16_t conn_handle, uint16_t chr_val_handle,
                              struct os_mbuf *txom);

/**
 * @deprecated Should not be used. Use ble_gatts_indicate_custom instead.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The value attribute handle of the
 *                                  characteristic to include in the outgoing
 *                                  indication.
 * @param txom                  The data to include in the indication.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_indicate_custom(uint16_t conn_handle, uint16_t chr_val_handle,
                              struct os_mbuf *txom);

/**
 * Sends a characteristic indication.  The content of the message is read from
 * the specified characteristic.
 *
 * @param conn_handle           The connection over which to execute the
 *                                  procedure.
 * @param chr_val_handle        The value attribute handle of the
 *                                  characteristic to include in the outgoing
 *                                  indication.
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gatts_indicate(uint16_t conn_handle, uint16_t chr_val_handle);

/**
 * @deprecated Should not be used. Use ble_gatts_indicate instead.
 * @copydoc ble_gatts_indicate
 */
int ble_gattc_indicate(uint16_t conn_handle, uint16_t chr_val_handle);

/**
 * Initialize the BLE GATT client
 *
 * @return                      0 on success; nonzero on failure.
 */
int ble_gattc_init(void);

/*** @server. */

struct ble_gatt_access_ctxt;

/** Type definition for GATT access callback function. */
typedef int ble_gatt_access_fn(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

/** Type definition for GATT characteristic flags. */
typedef uint16_t ble_gatt_chr_flags;

/** Represents the definition of a GATT characteristic. */
struct ble_gatt_chr_def {
    /**
     * Pointer to characteristic UUID; use BLE_UUIDxx_DECLARE macros to declare
     * proper UUID; NULL if there are no more characteristics in the service.
     */
    const ble_uuid_t *uuid;

    /**
     * Callback that gets executed when this characteristic is read or
     * written.
     */
    ble_gatt_access_fn *access_cb;

    /** Optional argument for callback. */
    void *arg;

    /**
     * Array of this characteristic's descriptors.  NULL if no descriptors.
     * Do not include CCCD; it gets added automatically if this
     * characteristic's notify or indicate flag is set.
     */
    struct ble_gatt_dsc_def *descriptors;

    /** Specifies the set of permitted operations for this characteristic. */
    ble_gatt_chr_flags flags;

    /** Specifies minimum required key size to access this characteristic. */
    uint8_t min_key_size;

    /**
     * At registration time, this is filled in with the characteristic's value
     * attribute handle.
     */
    uint16_t *val_handle;

    /** Client Presentation Format Descriptors */
    struct ble_gatt_cpfd *cpfd;
};

/** Represents the definition of a GATT service. */
struct ble_gatt_svc_def {
    /**
     * One of the following:
     *     o BLE_GATT_SVC_TYPE_PRIMARY - primary service
     *     o BLE_GATT_SVC_TYPE_SECONDARY - secondary service
     *     o 0 - No more services in this array.
     */
    uint8_t type;

    /**
     * Pointer to service UUID; use BLE_UUIDxx_DECLARE macros to declare
     * proper UUID; NULL if there are no more characteristics in the service.
     */
    const ble_uuid_t *uuid;

    /**
     * Array of pointers to other service definitions.  These services are
     * reported as "included services" during service discovery.  Terminate the
     * array with NULL.
     */
    const struct ble_gatt_svc_def **includes;

    /**
     * Array of characteristic definitions corresponding to characteristics
     * belonging to this service.
     */
    const struct ble_gatt_chr_def *characteristics;
};

/** Represents the definition of a GATT descriptor. */
struct ble_gatt_dsc_def {
    /**
     * Pointer to descriptor UUID; use BLE_UUIDxx_DECLARE macros to declare
     * proper UUID; NULL if there are no more characteristics in the service.
     */
    const ble_uuid_t *uuid;

    /** Specifies the set of permitted operations for this descriptor. */
    uint8_t att_flags;

    /** Specifies minimum required key size to access this descriptor. */
    uint8_t min_key_size;

    /** Callback that gets executed when the descriptor is read or written. */
    ble_gatt_access_fn *access_cb;

    /** Optional argument for callback. */
    void *arg;
};

/**
 * Client Presentation Format Descriptor
 * Defines the format of the Characteristic Value.
 *
 * +-------------+------------+
 * | Field Name  | Value Size |
 * +-------------+------------+
 * | Format      | 1 octet    |
 * | Exponent    | 1 octet    |
 * | Unit        | 2 octets   |
 * | Name Space  | 1 octet    |
 * | Description | 2 octets   |
 * +-------------+------------+
 */
struct ble_gatt_cpfd {
    /**
     * Format of the value of this characteristic. e.g. UINT32
     * Cannot be 0x00. 0x00 represents CPFD array is ended.
     */
    uint8_t format;

    /** Exponent field. Multiplies the value to 10^exponent. Type: sint8 */
    int8_t exponent;

    /** The unit of this characteristic. e.g. meters per second */
    uint16_t unit;

    /** The name space of the description. */
    uint8_t namespace;

    /** The description of this characteristic. Depends on name space. */
    uint16_t description;
};

/**
 * Context for an access to a GATT characteristic or descriptor.  When a client
 * reads or writes a locally registered characteristic or descriptor, an
 * instance of this struct gets passed to the application callback.
 */
struct ble_gatt_access_ctxt {
    /**
     * Indicates the gatt operation being performed.  This is equal to one of
     * the following values:
     *     o  BLE_GATT_ACCESS_OP_READ_CHR
     *     o  BLE_GATT_ACCESS_OP_WRITE_CHR
     *     o  BLE_GATT_ACCESS_OP_READ_DSC
     *     o  BLE_GATT_ACCESS_OP_WRITE_DSC
     */
    uint8_t op;

    /**
     * A container for the GATT access data.
     *     o For reads: The application populates this with the value of the
     *       characteristic or descriptor being read.
     *     o For writes: This is already populated with the value being written
     *       by the peer.  If the application wishes to retain this mbuf for
     *       later use, the access callback must set this pointer to NULL to
     *       prevent the stack from freeing it.
     */
    struct os_mbuf *om;

    /**
     * The GATT operation being performed dictates which field in this union is
     * valid.  If a characteristic is being accessed, the chr field is valid.
     * Otherwise a descriptor is being accessed, in which case the dsc field
     * is valid.
     */
    union {
        /**
         * The characteristic definition corresponding to the characteristic
         * being accessed.  This is what the app registered at startup.
         */
        const struct ble_gatt_chr_def *chr;

        /**
         * The descriptor definition corresponding to the descriptor being
         * accessed.  This is what the app registered at startup.
         */
        const struct ble_gatt_dsc_def *dsc;
    };

    /**
     * An offset in case of BLE_ATT_OP_READ_BLOB_REQ.
     * If the value is greater than zero it's an indication of a long attribute read.
     */
    uint16_t offset;
};

/**
 * Context passed to the registration callback; represents the GATT service,
 * characteristic, or descriptor being registered.
 */
struct ble_gatt_register_ctxt {
    /**
     * Indicates the gatt registration operation just performed.  This is
     * equal to one of the following values:
     *     o BLE_GATT_REGISTER_OP_SVC
     *     o BLE_GATT_REGISTER_OP_CHR
     *     o BLE_GATT_REGISTER_OP_DSC
     */
    uint8_t op;

    /**
     * The value of the op field determines which field in this union is valid.
     */
    union {
        /** Service; valid if op == BLE_GATT_REGISTER_OP_SVC. */
        struct {
            /** The ATT handle of the service definition attribute. */
            uint16_t handle;

            /**
             * The service definition representing the service being
             * registered.
             */
            const struct ble_gatt_svc_def *svc_def;
        } svc;

        /** Characteristic; valid if op == BLE_GATT_REGISTER_OP_CHR. */
        struct {
            /** The ATT handle of the characteristic definition attribute. */
            uint16_t def_handle;

            /** The ATT handle of the characteristic value attribute. */
            uint16_t val_handle;

            /**
             * The characteristic definition representing the characteristic
             * being registered.
             */
            const struct ble_gatt_chr_def *chr_def;

            /**
             * The service definition corresponding to the characteristic's
             * parent service.
             */
            const struct ble_gatt_svc_def *svc_def;
        } chr;

        /** Descriptor; valid if op == BLE_GATT_REGISTER_OP_DSC. */
        struct {
            /** The ATT handle of the descriptor definition attribute. */
            uint16_t handle;

            /**
             * The descriptor definition corresponding to the descriptor being
             * registered.
             */
            const struct ble_gatt_dsc_def *dsc_def;

            /**
             * The characteristic definition corresponding to the descriptor's
             * parent characteristic.
             */
            const struct ble_gatt_chr_def *chr_def;

            /**
             * The service definition corresponding to the descriptor's
             * grandparent service
             */
            const struct ble_gatt_svc_def *svc_def;
        } dsc;
    };
};

/** Type definition for GATT registration callback function. */
typedef void ble_gatt_register_fn(struct ble_gatt_register_ctxt *ctxt,
                                  void *arg);

/**
 * Queues a set of service definitions for registration.  All services queued
 * in this manner get registered when ble_gatts_start() is called.
 *
 * @param svcs                  An array of service definitions to queue for
 *                                  registration.  This array must be
 *                                  terminated with an entry whose 'type'
 *                                  equals 0.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOMEM on heap exhaustion.
 */
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *svcs);

/**
 * Set visibility of local GATT service. Invisible services are not removed
 * from database but are not discoverable by peer devices. Service Changed
 * should be handled by application when needed by calling
 * ble_svc_gatt_changed().
 *
 * @param handle                Handle of service
 * @param visible               non-zero if service should be visible
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOENT if service wasn't found.
 */
int ble_gatts_svc_set_visibility(uint16_t handle, int visible);

/**
 * Adjusts a host configuration object's settings to accommodate the specified
 * service definition array.  This function adds the counts to the appropriate
 * fields in the supplied configuration object without clearing them first, so
 * it can be called repeatedly with different inputs to calculate totals.  Be
 * sure to zero the GATT server settings prior to the first call to this
 * function.
 *
 * @param defs                  The service array containing the resource
 *                                  definitions to be counted.
 *
 * @return                      0 on success;
 *                              BLE_HS_EINVAL if the svcs array contains an
 *                                  invalid resource definition.
 */
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *defs);

/**
 * Send notification (or indication) to any connected devices that have
 * subscribed for notification (or indication) for specified characteristic.
 *
 * @param chr_val_handle        Characteristic value handle
 */
void ble_gatts_chr_updated(uint16_t chr_val_handle);

/**
 * Retrieves the attribute handle associated with a local GATT service.
 *
 * @param uuid                  The UUID of the service to look up.
 * @param out_handle            On success, populated with the handle of the
 *                                  service attribute.  Pass null if you don't
 *                                  need this value.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOENT if the specified service could
 *                                  not be found.
 */
int ble_gatts_find_svc(const ble_uuid_t *uuid, uint16_t *out_handle);

/**
 * Retrieves the pair of attribute handles associated with a local GATT
 * characteristic.
 *
 * @param svc_uuid              The UUID of the parent service.
 * @param chr_uuid              The UUID of the characteristic to look up.
 * @param out_def_handle        On success, populated with the handle
 *                                  of the characteristic definition attribute.
 *                                  Pass null if you don't need this value.
 * @param out_val_handle        On success, populated with the handle
 *                                  of the characteristic value attribute.
 *                                  Pass null if you don't need this value.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOENT if the specified service or
 *                                  characteristic could not be found.
 */
int ble_gatts_find_chr(const ble_uuid_t *svc_uuid, const ble_uuid_t *chr_uuid,
                       uint16_t *out_def_handle, uint16_t *out_val_handle);

/**
 * Retrieves the attribute handle associated with a local GATT descriptor.
 *
 * @param svc_uuid              The UUID of the grandparent service.
 * @param chr_uuid              The UUID of the parent characteristic.
 * @param dsc_uuid              The UUID of the descriptor ro look up.
 * @param out_dsc_handle        On success, populated with the handle
 *                                  of the descriptor attribute.  Pass null if
 *                                  you don't need this value.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOENT if the specified service,
 *                                  characteristic, or descriptor could not be
 *                                  found.
 */
int ble_gatts_find_dsc(const ble_uuid_t *svc_uuid, const ble_uuid_t *chr_uuid,
                       const ble_uuid_t *dsc_uuid, uint16_t *out_dsc_handle);

/** Type definition for GATT service iteration callback function. */
typedef void (*ble_gatt_svc_foreach_fn)(const struct ble_gatt_svc_def *svc,
                                        uint16_t handle,
                                        uint16_t end_group_handle,
                                        void *arg);

/**
 * Prints dump of local GATT database. This is useful to log local state of
 * database in human readable form.
 */
void ble_gatts_show_local(void);

/**
 * Resets the GATT server to its initial state.  On success, this function
 * removes all supported services, characteristics, and descriptors.  This
 * function requires that:
 *     o No peers are connected, and
 *     o No GAP operations are active (advertise, discover, or connect).
 *
 * @return                      0 on success;
 *                              BLE_HS_EBUSY if the GATT server could not be
 *                                  reset due to existing connections or active
 *                                  GAP procedures.
 */
int ble_gatts_reset(void);

/**
 * Makes all registered services available to peers.  This function gets called
 * automatically by the NimBLE host on startup; manual calls are only necessary
 * for replacing the set of supported services with a new one.  This function
 * requires that:
 *     o No peers are connected, and
 *     o No GAP operations are active (advertise, discover, or connect).
 *
 * @return                      0 on success;
 *                              A BLE host core return code on unexpected
 *                                  error.
 */
int ble_gatts_start(void);

/**
 * Gets Client Supported Features for specified connection.
 *
 * @param conn_handle           Connection handle identifying the connection for
 *                                  which Client Supported Features should be saved
 * @param out_supported_feat    Client supported features to be returned.
 * @param len                   The size of the Client Supported Features
 *                                  characteristic in octets.
 *
 * @return                      0 on success;
 *                              BLE_HS_ENOTCONN if no matching connection
 *                                  was found
 *                              BLE_HS_EINVAL if supplied buffer is empty or
 *                                  if any Client Supported Feature was
 *                                  attempted to be disabled.
 *                              A BLE host core return code on unexpected error.
 *
 */
int ble_gatts_peer_cl_sup_feat_get(uint16_t conn_handle, uint8_t *out_supported_feat, uint8_t len);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* H_BLE_GATT_ */
