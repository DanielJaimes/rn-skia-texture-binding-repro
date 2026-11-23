package com.skiarepro

import com.facebook.react.BaseReactPackage
import com.facebook.react.bridge.*
import com.facebook.react.module.model.ReactModuleInfo
import com.facebook.react.module.model.ReactModuleInfoProvider

class RBHelperModule(ctx: ReactApplicationContext) : ReactContextBaseJavaModule(ctx) {
  override fun getName() = "RBHelper"

  // kind: "gpu" | "cpu". Resolves [hi, lo] (32-bit halves of the AHardwareBuffer pointer).
  @ReactMethod
  fun make(kind: String, promise: Promise) {
    val r = nativeMake(kind == "cpu", 512, 512)
    promise.resolve(Arguments.createArray().apply { pushDouble(r[0].toDouble()); pushDouble(r[1].toDouble()) })
  }

  companion object {
    init { System.loadLibrary("rbhelper") }
    @JvmStatic external fun nativeMake(cpu: Boolean, w: Int, h: Int): LongArray
  }
}

class RBHelperPackage : BaseReactPackage() {
  override fun getModule(name: String, c: ReactApplicationContext): NativeModule? =
    if (name == "RBHelper") RBHelperModule(c) else null

  override fun getReactModuleInfoProvider() = ReactModuleInfoProvider {
    mapOf("RBHelper" to ReactModuleInfo("RBHelper", RBHelperModule::class.java.name, false, false, false, false))
  }
}
