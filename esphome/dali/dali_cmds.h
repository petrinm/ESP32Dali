#pragma once

// OUTPUT LEVEL INSTRUCTIONS
#define DALI_CMD_OFF                            0x0000 // 0 - Switches off lamp(s)
#define DALI_CMD_UP                             0x0001 // 1 - Increases lamp(s) illumination level
#define DALI_CMD_DOWN                           0x0002 // 2 - Decreases lamp(s) illumination level
#define DALI_CMD_STEP_UP                        0x0003 // 3 - Increases the target illumination level by 1
#define DALI_CMD_STEP_DOWN                      0x0004 // 4 - Decreases the target illumination level by 1
#define DALI_CMD_RECALL_MAX_LEVEL               0x0005 // 5 - Changes the current light output to the maximum level
#define DALI_CMD_RECALL_MIN_LEVEL               0x0006 // 6 - Changes the current light output to the minimum level
#define DALI_CMD_STEP_DOWN_AND_OFF              0x0007 // 7 - If the target level is zero, lamp(s) are turned off; if the target level is between the min. and max. levels, decrease the target level by one; if the target level is max., lamp(s) are turned off
#define DALI_CMD_ON_AND_STEP_UP                 0x0008 // 8 - If the target level is zero, lamp(s) are set to minimum level; if target level is between min. and max. levels, increase the target level by one
#define DALI_CMD_ENABLE_DAPC_SEQUENCE           0x0009 // 9 - Indicates the start of DAPC (level) commands
#define DALI_CMD_GO_TO_LAST_ACTIVE_LEVEL        0x000A // 10 - Sets the target level to the last active output level
#define DALI_CMD_GO_TO_SCENE                    0x0010 // 16 - Sets a group of lamps to a predefined scene
// CONFIGURATION INSTRUCTIONS
#define DALI_CMD_DALI_RESET                     0x0020 // 32 - Configures all variables back to their Reset state
#define DALI_CMD_STORE_ACTUAL_LEVEL_IN          0x0021 // 33 - Stores the actual level value into Data Transfer Register 0 (DTR0)
#define DALI_CMD_SAVE_PERSISTENT_VARIABLES      0x0022 // 34 - Stores all variables into Nonvolatile Memory (NVM)
#define DALI_CMD_SET_OPERATING_MODE             0x0023 // 35 - Sets the operating mode to the value listed in DTR0
#define DALI_CMD_RESET_MEMORY_BANK              0x0024 // 36 - Resets the memory bank identified by DTR0 (memory bank must be implemented and unlocked)
#define DALI_CMD_IDENTIFY_DEVICE                0x0025 // 37 - Instructs a control gear to run an identification procedure
#define DALI_CMD_SET_MAX_LEVEL                  0x002A // 42 - Configures the control gear's maximum output level to the value stored in DTR0
#define DALI_CMD_SET_MIN_LEVEL                  0x002B // 43 - Configures the control gear's minimum output level to the value stored in DTR0
#define DALI_CMD_SET_SYSTEM_FAILURE_LEVEL       0x002C // 44 - Sets the control gear's output level in the event of a system failure to the value stored in DTR0
#define DALI_CMD_SET_POWER_ON_LEVEL             0x002D // 45 - Configures the output level upon power-up based on the value of DTR0
#define DALI_CMD_SET_FADE_TIME                  0x002E // 46 - Sets the fade time based on the value of DTR0
#define DALI_CMD_SET_FADE_RATE                  0x002F // 47 - Sets the fade rate based on the value of DTR0
#define DALI_CMD_SET_EXTENDED_FADE_TIME         0x0030 // 48 - Sets the extended fade rate based on the value of DTR0; used when fade time = 0
#define DALI_CMD_SET_SCENE                      0x0040 // 64-79 - Configures scene 'x' based on the value of DTR0
#define DALI_CMD_REMOVE_FROM_SCENE              0x0050 // 80-95 - Removes one of the control gears from a scene
#define DALI_CMD_ADD_TO_GROUP                   0x0060 // 96-111 - Adds a control gear to a group
#define DALI_CMD_REMOVE_FROM_GROUP              0x0070 // 112-127 - Removes a control gear from a group
#define DALI_CMD_SET_SHORT_ADDRESS              0x0080 // 128 - Sets a control gear's short address to the value of DTR0
#define DALI_CMD_ENABLE_WRITE_MEMORY            0x0081 // 129 - Allows writing into memory banks
// QUERY INSTRUCTIONS
#define DALI_CMD_QUERY_STATUS                   0x0090 // 144 - Determines the control gear's status based on a combination of gear properties
#define DALI_CMD_QUERY_CONTROL_GEAR_PRESENT     0x0091 // 145 - Determines if a control gear is present
#define DALI_CMD_QUERY_LAMP_FAILURE             0x0092 // 146 - Determines if a lamp has failed
#define DALI_CMD_QUERY_LAMP_POWER_ON            0x0093 // 147 - Determines if a lamp is On
#define DALI_CMD_QUERY_LIMIT_ERROR              0x0094 // 148 - Determines if the requested target level has been modified due to max. or min. level limitations
#define DALI_CMD_QUERY_RESET_STATE              0x0095 // 149 - Determines if all NVM variables are in their Reset state
#define DALI_CMD_QUERY_MISSING_SHORT_ADDRESS    0x0096 // 150 - Determines if a control gear's address is equal to 0xFF // define DALI_CMD_QUERY_VERSION NUMBER 0x97 // Returns the device's version number located in memory bank 0, location 0x16 // define DALI_CMD_QUERY_CONTENT DTR0 0x98 // Returns the value of DTR0
#define DALI_CMD_QUERY_VERSION_NUMBER           0x0095 // 151 - Returns the corresponding IEC standard number
#define DALI_CMD_QUERY_CONTENT                  0x0095 // 152 - What is the DTR content
#define DALI_CMD_QUERY_DEVICE_TYPE              0x0099 // 153 - Determines the device type supported by the control gear
#define DALI_CMD_QUERY_PHYSICAL_MINIMUM         0x009A // 154 - Returns the minimum light output that the control gear can operate at
#define DALI_CMD_QUERY_POWER_FAILURE            0x009B // 155 - Determines if an external power cycle occurred
#define DALI_CMD_QUERY_CONTENT_DTR1             0x009C // 156 - Returns the value of DTR1
#define DALI_CMD_QUERY_CONTENT_DTR2             0x009D // 157 - Returns the value of DTR2
#define DALI_CMD_QUERY_OPERATING_MODE           0x009E // 158 - Determines the control gear's operating mode
#define DALI_CMD_QUERY_LIGHT_SOURCE_TYPE        0x009F // 159 - Returns the control gear's type of light source
#define DALI_CMD_QUERY_ACTUAL_LEVEL             0x00A0 // 160 - Returns the control gear's actual power output level
#define DALI_CMD_QUERY_MAX_LEVEL                0x00A1 // 161 - Returns the control gear's maximum output setting
#define DALI_CMD_QUERY_MIN_LEVEL                0x00A2 // 162 - Returns the control gear's minimum output setting
#define DALI_CMD_QUERY_POWER_ON_LEVEL           0x00A3 // 163 - Returns the value of the intensity level upon power-up
#define DALI_CMD_QUERY_SYSTEM_FAILURE_LEVEL     0x00A4 // 164 - Returns the value of the intensity level due to a system failure
#define DALI_CMD_QUERY_FADE_TIME_FADE_RATE      0x00A5 // 165 - Returns a byte in which the upper nibble is equal to the fade time value and the lower nibble is the fade rate value
#define DALI_CMD_QUERY_MANUFACTURER_SPECIFIC_MODE 0x00A6 // 166 - Returns a 'YES' when the operating mode is within the range of 0x80 //  0xFF // define DALI_CMD_QUERY_NEXT DEVICE TYPE 0xA7 // Determines if the control gear has more than one feature, and if so, return the first/next device type or feature
#define DALI_CMD_QUERY_EXTENDED_FADE_TIME       0x00A8 // 168 - Returns a byte in which bits 6-4 is the value of the extended fade time multiplier and the lower nibble is the extended fade time base
#define DALI_CMD_QUERY_CONTROL_GEAR_FAILURE     0x00AA // 169 - Determines if a control gear has failed
#define DALI_CMD_QUERY_SCENE_LEVEL              0x00B0 // 176 - Returns the level value of scene 'x'
#define DALI_CMD_QUERY_GROUPS_0_7               0x00C0 // 192 - Returns a byte in which each bit represents a member of a group. A '1' represents a member of the group
#define DALI_CMD_QUERY_GROUPS_8_15              0x00C1 // 193 - Returns a byte in which each bit represents a member of a group. A '1' represents a member of the group
#define DALI_CMD_QUERY_RANDOM_ADDRESS_H         0x00C2 // 194 - Returns the upper byte of a randomly generated address
#define DALI_CMD_QUERY_RANDOM_ADDRESS_M         0x00C3 // 195 - Returns the high byte of a randomly generated address
#define DALI_CMD_QUERY_RANDOM_ADDRESS_L         0x00C4 // 196 - Returns the low byte of a randomly generated address
#define DALI_CMD_READ_MEMORY_LOCATION           0x00C5 // 197 - Returns the content of the memory location stored in DTR0 that is located within the memory bank listed in DTR1
#define DALI_CMD_QUERY_EXTENDED_VERSION_NUMBER  0x00FF // 255 - Returns the version number belonging to the device type or feature
// SPECIAL COMMANDS
#define DALI_CMD_TERMINATE                      0x01A1 // 257 - Stops the control gear's initilization
#define DALI_CMD_DTR0_DATA                      0x01A3 // 257 - Loads a data byte into DTR0
#define DALI_CMD_INITIALISE                     0x01A5 // 258 - Initializes a control gear, command must be issued twice
#define DALI_CMD_RANDOMIZE                      0x01A7 // 259 - Generates a random address value, command must be issued twice
#define DALI_CMD_COMPARE                        0x01A9 // 2 - Compares the random address variable to the search address variable
#define DALI_CMD_WITHDRAW                       0x01AB // 2 - Changes the initialization state to reflect that a control gear had been identified but remains in the initialization state
#define DALI_CMD_PING                           0x01AD // 2 - Used by control devices to indicate theirm presence on the bus
#define DALI_CMD_SEARCH_ADDRH                   0x01B1 // 2 - Determines if an address is present on the bus
#define DALI_CMD_SEARCH_ADDRM                   0x01B3 // 2 - Determines if an address is present on the bus
#define DALI_CMD_SEARCH_ADDRL                   0x01B5 // 2 - Determines if an address is present on the bus
#define DALI_CMD_PROGRAM_SHORT_ADDRESS          0x01B7 // 2 - Programs a control gear's short address
#define DALI_CMD_VERIFY_SHORT_ADDRESS           0x01B9 // 2 - Verifies if a control gear's short address is correct
#define DALI_CMD_QUERY_SHORT_ADDRESS            0x01BB // 2 - Queries a control gear's short address
#define DALI_CMD_ENABLE_DEVICE_TYPE             0x01C1 // 2 - Enables a control gear's device type function
#define DALI_CMD_DTR1_DATA                      0x01C3 // 2 - Loads a data byte into DTR1
#define DALI_CMD_DTR2_DATA                      0x01C5 // 2 - Loads a data byte into DTR2
#define DALI_CMD_WRITE_MEMORY_LOCATION          0x01C7 // 2 - Writes data into a specific memory location and returns the value of the data written
#define DALI_CMD_WRITE_MEMORY_LOCATION_NO_REPLY 0x01C9 // 2 - Writes data into a specific memory location but does not return a response

