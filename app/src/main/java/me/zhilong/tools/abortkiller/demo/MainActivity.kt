package me.zhilong.tools.abortkiller.demo

import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import androidx.appcompat.app.AppCompatActivity
import com.alibaba.android.patronus.Patrons
import com.alibaba.android.patronus.Patrons.PatronsConfig
import kotlin.system.measureTimeMillis

class MainActivity : AppCompatActivity() {
    val MB = 1024 * 1024

    val manyString = ArrayList<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        findViewById<Button>(R.id.resize)?.text = "虚拟内存压缩 (${Patrons.readVssSize() / MB}mb)"
    }

    fun onClick(v: View?) {
        v as Button

        when (v.id) {
            R.id.abort -> {
                abort()
            }
            R.id.java_alloc -> {
                repeat(25 * 1024) {
                    manyString.add(createLongString(1024))
                }
            }
            R.id.native_alloc -> {
                native_alloc()
            }
            R.id.get_region_space -> {
                v.text = "Region Space = ${Patrons.getRegionSpaceSize() / MB} mb"
            }
            R.id.dump -> {
                Log.e("Patrons-Demo", "\n" + Patrons.dumpNativeLogs())
            }
            R.id.resize -> {
                v.setBackgroundColor(
                    if (Patrons.shrinkRegionSpace((Patrons.getRegionSpaceSize() / MB).toInt() - 125)) {
                        Color.GREEN
                    } else {
                        Color.RED
                    }
                )

                v.text = "虚拟内存压缩 (${Patrons.readVssSize() / MB}mb)"
            }
            R.id.init -> {
                val config = PatronsConfig()
                config.periodOfCheck = 10
                config.periodOfShrink = 0.5f
                config.debuggable = true

                val code: Int

                val context = this

                val init_cost = measureTimeMillis {
                    code = Patrons.init(context, config)
                }

                Log.e("Patrons-Demo", "Patrons init cost $init_cost ms")

                if (code == 0) {
                    v.setBackgroundColor(Color.GREEN)
                    v.text = "初始化成功"
                } else {
                    v.setBackgroundColor(Color.RED)
                    v.text = "初始化失败 ($code)"
                }
            }
            else -> {

            }
        }
    }

    private fun createLongString(length: Int): String {
        val sb = StringBuilder(length)
        for (i in 0 until length) sb.append('a')
        sb.append(System.nanoTime())
        return sb.toString()
    }

    private external fun native_alloc()

    private external fun abort()

    companion object {
        // Used to load the 'native-lib' library on application startup.
        init {
            System.loadLibrary("memory-alloc")
        }
    }
}