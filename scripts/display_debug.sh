#!/bin/bash

# FBC_CFB_BASE 0x43200
../tools/intel_reg_read 0x43200
# FBC_CTL 0x43208
../tools/intel_reg_read 0x43208
# ERR_INT 0x44040
../tools/intel_reg_read 0x44040
# DE_RRMR 0x44050
../tools/intel_reg_read 0x44050
# ARB_CTL 0x45000
../tools/intel_reg_read 0x45000
# ARB_CTL2 0x45004
../tools/intel_reg_read 0x45004
# MSG_CTL 0x45010
../tools/intel_reg_read 0x45010
# Watermarks
../tools/intel_reg_read 0x45100
../tools/intel_reg_read 0x45104
../tools/intel_reg_read 0x45200
../tools/intel_reg_read 0x45108
../tools/intel_reg_read 0x4510C
../tools/intel_reg_read 0x45110
../tools/intel_reg_read 0x45120
../tools/intel_reg_read 0x45124
../tools/intel_reg_read 0x45128
# Pipe A timing 0x60000-0x6004C
../tools/intel_reg_read 0x60000 -c 0x13;
# Pipe B timing 0x61000-0x6104C
../tools/intel_reg_read 0x61000 -c 0x13;
# Pipe C timing 0x62000-0x6204C
../tools/intel_reg_read 0x62000 -c 0x13;
# FDI A 0x60100
# FDI B 0x61100
# FDI C 0x62100
# EDP 0x64000
../tools/intel_reg_read 0x60100
../tools/intel_reg_read 0x61100
../tools/intel_reg_read 0x62100
../tools/intel_reg_read 0x64000
# Panel fitter A window size 0x68074
# Panel fitter A control 0x68080
../tools/intel_reg_read 0x68074
../tools/intel_reg_read 0x68080
# Panel fitter B window size 0x68874
# Panel fitter B control 0x68880
../tools/intel_reg_read 0x68874
../tools/intel_reg_read 0x68880
# Panel fitter C window size 0x69074
# Panel fitter C control 0x69080
../tools/intel_reg_read 0x69074
../tools/intel_reg_read 0x69080
# Pipe A config 0x70008
# Pipe B config 0x71008
# Pipe C config 0x72008
../tools/intel_reg_read 0x70008
../tools/intel_reg_read 0x71008
../tools/intel_reg_read 0x72008
# Cursor A control 0x70080
# Cursor B control 0x71080
# Cursor C control 0x72080
../tools/intel_reg_read 0x70080
../tools/intel_reg_read 0x71080
../tools/intel_reg_read 0x72080
# Primary A control 0x70180
# Primary B control 0x71180
# Primary C control 0x72180
../tools/intel_reg_read 0x70180
../tools/intel_reg_read 0x71180
../tools/intel_reg_read 0x72180
# Sprite A control 0x70280
# Sprite B control 0x71280
# Sprite C control 0x72280
../tools/intel_reg_read 0x70280
../tools/intel_reg_read 0x71280
../tools/intel_reg_read 0x72280
# Sprite A size 0x70290
# Sprite B size 0x71290
# Sprite C size 0x72290
../tools/intel_reg_read 0x70290
../tools/intel_reg_read 0x71290
../tools/intel_reg_read 0x72290
# Sprite A scaling 0x70304
# Sprite B scaling 0x71304
# Sprite C scaling 0x72304
../tools/intel_reg_read 0x70304
../tools/intel_reg_read 0x71304
../tools/intel_reg_read 0x72304
# PCH DE Interrupt enable 0xC400C
../tools/intel_reg_read 0xC400C
# PCH DE Interrupt IIR 0xC4008
../tools/intel_reg_read 0xC4008
# PCH DE hotplug 0xC4030
../tools/intel_reg_read 0xC4030
# SERR_INT 0xC4040
../tools/intel_reg_read 0xC4040
# PCH DPLL A CTL 0xC6014
# PCH DPLL A Divisor 0 0xC6040
# PCH DPLL A Divisor 1 0xC6044
../tools/intel_reg_read 0xC6014
../tools/intel_reg_read 0xC6040
../tools/intel_reg_read 0xC6044
# PCH DPLL B CTL 0xC6018
# PCH DPLL B Divisor 0 0xC6048
# PCH DPLL B Divisor 1 0xC604C
../tools/intel_reg_read 0xC6018
../tools/intel_reg_read 0xC6048
../tools/intel_reg_read 0xC604C
# PCH DPLL DREF CTL 0xC6200
../tools/intel_reg_read 0xC6200
# PCH DPLL SEL 0xC7000
../tools/intel_reg_read 0xC7000
# PCH Panel Status 0xC7200
../tools/intel_reg_read 0xC7200
# PCH Panel Control 0xC7204
../tools/intel_reg_read 0xC7204
# Transcoder A timing 0xE0000-0xE004F
# Transcoder B timing 0xE1000-0xE104F
# Transcoder C timing 0xE2000-0xE204F
../tools/intel_reg_read 0xE0000 -c 0x14;
../tools/intel_reg_read 0xE1000 -c 0x14;
../tools/intel_reg_read 0xE2000 -c 0x14;
# Transcoder A DP CTL 0xE0300
# Transcoder B DP CTL 0xE1300
# Transcoder C DP CTL 0xE2300
../tools/intel_reg_read 0xE0300
../tools/intel_reg_read 0xE1300
../tools/intel_reg_read 0xE2300
# CRT DAC CTL 0xE1100
../tools/intel_reg_read 0xE1100
# HDMI/DVI B CTL 0xE1140
# HDMI/DVI C CTL 0xE1150
# HDMI/DVI D CTL 0xE1160
../tools/intel_reg_read 0xE1140
../tools/intel_reg_read 0xE1150
../tools/intel_reg_read 0xE1160
# LVDS 0xE1180
../tools/intel_reg_read 0xE1180
# DP B CTL 0xE4100
# DP C CTL 0xE4200
# DP D CTL 0xE4300
../tools/intel_reg_read 0xE4100
../tools/intel_reg_read 0xE4200
../tools/intel_reg_read 0xE4300
# Transcoder A config 0xF0008
# FDI RX A CTL 0xF000C
# FDI RX A MISC 0xF0010
# FDI RX A IIR 0xF0014
# FDI RX A IMR 0xF0018
../tools/intel_reg_read 0xF0008 -c 5;
# Transcoder B config 0xF1008
# FDI RX B CTL 0xF100C
# FDI RX B MISC 0xF1010
# FDI RX B IIR 0xF1014
# FDI RX B IMR 0xF1018
../tools/intel_reg_read 0xF1008 -c 5;
# Transcoder C config 0xF2008
# FDI RX C CTL 0xF200C
# FDI RX C MISC 0xF2010
# FDI RX C IIR 0xF2014
# FDI RX C IMR 0xF2018
../tools/intel_reg_read 0xF2008 -c 5;
#Check if frame and line counters are running
../tools/intel_reg_read 0x44070
../tools/intel_reg_read 0x70050
../tools/intel_reg_read 0x71050
../tools/intel_reg_read 0x72050
sleep 2;
../tools/intel_reg_read 0x44070
../tools/intel_reg_read 0x70050
../tools/intel_reg_read 0x71050
../tools/intel_reg_read 0x72050
