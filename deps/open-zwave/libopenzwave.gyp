{
  "targets": [
    {
      "target_name": "libopenzwave",
      "product_prefix": "lib",
      "type": "static_library",
      "sources": [
        "cpp/tinyxml/tinystr.cpp",
        "cpp/tinyxml/tinyxml.cpp",
        "cpp/tinyxml/tinyxmlerror.cpp",
        "cpp/tinyxml/tinyxmlparser.cpp",
        "cpp/src/aes/aes_modes.c",
        "cpp/src/aes/aescrypt.c",
        "cpp/src/aes/aeskey.c",
        "cpp/src/aes/aestab.c",
        "cpp/src/command_classes/Alarm.cpp",
        "cpp/src/command_classes/ApplicationStatus.cpp",
        "cpp/src/command_classes/Association.cpp",
        "cpp/src/command_classes/AssociationCommandConfiguration.cpp",
        "cpp/src/command_classes/Basic.cpp",
        "cpp/src/command_classes/BasicWindowCovering.cpp",
        "cpp/src/command_classes/Battery.cpp",
        "cpp/src/command_classes/CRC16Encap.cpp",
        "cpp/src/command_classes/ClimateControlSchedule.cpp",
        "cpp/src/command_classes/Clock.cpp",
        "cpp/src/command_classes/CommandClass.cpp",
        "cpp/src/command_classes/CommandClasses.cpp",
        "cpp/src/command_classes/Configuration.cpp",
        "cpp/src/command_classes/ControllerReplication.cpp",
        "cpp/src/command_classes/DoorLock.cpp",
        "cpp/src/command_classes/DoorLockLogging.cpp",
        "cpp/src/command_classes/EnergyProduction.cpp",
        "cpp/src/command_classes/Hail.cpp",
        "cpp/src/command_classes/Indicator.cpp",
        "cpp/src/command_classes/Language.cpp",
        "cpp/src/command_classes/Lock.cpp",
        "cpp/src/command_classes/ManufacturerSpecific.cpp",
        "cpp/src/command_classes/Meter.cpp",
        "cpp/src/command_classes/MeterPulse.cpp",
        "cpp/src/command_classes/MultiCmd.cpp",
        "cpp/src/command_classes/MultiInstance.cpp",
        "cpp/src/command_classes/MultiInstanceAssociation.cpp",
        "cpp/src/command_classes/NoOperation.cpp",
        "cpp/src/command_classes/NodeNaming.cpp",
        "cpp/src/command_classes/Powerlevel.cpp",
        "cpp/src/command_classes/Proprietary.cpp",
        "cpp/src/command_classes/Protection.cpp",
        "cpp/src/command_classes/SceneActivation.cpp",
        "cpp/src/command_classes/Security.cpp",
        "cpp/src/command_classes/SensorAlarm.cpp",
        "cpp/src/command_classes/SensorBinary.cpp",
        "cpp/src/command_classes/SensorMultilevel.cpp",
        "cpp/src/command_classes/SwitchAll.cpp",
        "cpp/src/command_classes/SwitchBinary.cpp",
        "cpp/src/command_classes/SwitchMultilevel.cpp",
        "cpp/src/command_classes/SwitchToggleBinary.cpp",
        "cpp/src/command_classes/SwitchToggleMultilevel.cpp",
        "cpp/src/command_classes/ThermostatFanMode.cpp",
        "cpp/src/command_classes/ThermostatFanState.cpp",
        "cpp/src/command_classes/ThermostatMode.cpp",
        "cpp/src/command_classes/ThermostatOperatingState.cpp",
        "cpp/src/command_classes/ThermostatSetpoint.cpp",
        "cpp/src/command_classes/TimeParameters.cpp",
        "cpp/src/command_classes/UserCode.cpp",
        "cpp/src/command_classes/Version.cpp",
        "cpp/src/command_classes/WakeUp.cpp",
        "cpp/src/value_classes/Value.cpp",
        "cpp/src/value_classes/ValueBool.cpp",
        "cpp/src/value_classes/ValueButton.cpp",
        "cpp/src/value_classes/ValueByte.cpp",
        "cpp/src/value_classes/ValueDecimal.cpp",
        "cpp/src/value_classes/ValueInt.cpp",
        "cpp/src/value_classes/ValueList.cpp",
        "cpp/src/value_classes/ValueRaw.cpp",
        "cpp/src/value_classes/ValueSchedule.cpp",
        "cpp/src/value_classes/ValueShort.cpp",
        "cpp/src/value_classes/ValueStore.cpp",
        "cpp/src/value_classes/ValueString.cpp",
        "cpp/src/platform/Controller.cpp",
        "cpp/src/platform/Event.cpp",
        "cpp/src/platform/FileOps.cpp",
        "cpp/src/platform/HidController.cpp",
        "cpp/src/platform/Log.cpp",
        "cpp/src/platform/Mutex.cpp",
        "cpp/src/platform/SerialController.cpp",
        "cpp/src/platform/Stream.cpp",
        "cpp/src/platform/Thread.cpp",
        "cpp/src/platform/TimeStamp.cpp",
        "cpp/src/platform/Wait.cpp",
        "cpp/src/platform/unix/EventImpl.cpp",
        "cpp/src/platform/unix/FileOpsImpl.cpp",
        "cpp/src/platform/unix/LogImpl.cpp",
        "cpp/src/platform/unix/MutexImpl.cpp",
        "cpp/src/platform/unix/SerialControllerImpl.cpp",
        "cpp/src/platform/unix/ThreadImpl.cpp",
        "cpp/src/platform/unix/TimeStampImpl.cpp",
        "cpp/src/platform/unix/WaitImpl.cpp",
        "cpp/src/Driver.cpp",
        "cpp/src/Group.cpp",
        "cpp/src/Manager.cpp",
        "cpp/src/Msg.cpp",
        "cpp/src/Node.cpp",
        "cpp/src/Options.cpp",
        "cpp/src/Scene.cpp",
        "cpp/src/Utils.cpp",
        "cpp/src/vers.cpp"
      ],
      "include_dirs": [
        "cpp/hidapi/hidapi",
        "cpp/src",
        "cpp/src/command_classes",
        "cpp/src/platform",
        "cpp/src/platform/unix",
        "cpp/src/value_classes",
        "cpp/tinyxml"
      ],
      "configurations": {
        "Release": {
          "cflags": [
            "-Wno-ignored-qualifiers",
            "-Wno-tautological-undefined-compare",
            "-Wno-unknown-pragmas"
          ],
          "xcode_settings": {
            "OTHER_CFLAGS": [
              "-Wno-ignored-qualifiers",
              "-Wno-tautological-undefined-compare",
              "-Wno-unknown-pragmas"
            ]
          }
        }
      },
      "conditions": [
        ['OS=="linux"', {
          "sources": [
            "cpp/hidapi/linux/hid.c"
          ]
        }],
        ['OS=="mac"', {
          "sources": [
            "cpp/hidapi/mac/hid.c"
          ],
          "defines": [
            "DARWIN"
          ]
        }]
      ]
    }
  ]
}
