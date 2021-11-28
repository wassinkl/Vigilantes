package edu.umich.Vigilantes

import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.util.Log
import android.view.View
import android.view.WindowManager
import android.widget.ImageView
import android.widget.PopupWindow
import android.widget.Toast
import java.io.File

fun Context.toast(message: String, short: Boolean = true) {
    Toast.makeText(this, message, if (short) Toast.LENGTH_SHORT else Toast.LENGTH_LONG).show()
}
fun Context.dp2px(dp: Float): Int {
    return Math.ceil((dp * resources.displayMetrics.density).toDouble()).toInt()
}

fun ImageView.display(uri: Uri) {
    setImageURI(uri)
    visibility = View.VISIBLE
}
fun Uri.toFile(context: Context): File? {

    if (!(authority == "media" || authority == "com.google.android.apps.photos.contentprovider")) {
        // for on-device media files only
        context.toast("Media file not on device")
        Log.d("Uri.toFile", authority.toString())
        return null
    }

    if (scheme.equals("content")) {
        var cursor: Cursor? = null
        try {
            cursor = context.getContentResolver().query(
                this, arrayOf("_data"),
                null, null, null
            )

            cursor?.run {
                moveToFirst()
                return File(getString(getColumnIndexOrThrow("_data")))
            }
        } finally {
            cursor?.close()
        }
    }
    return null
}

fun PopupWindow.dimBehind() {
    val container = contentView.rootView
    val context = contentView.context
    val wm = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
    val p = container.layoutParams as WindowManager.LayoutParams
    p.flags = p.flags or WindowManager.LayoutParams.FLAG_DIM_BEHIND
    p.dimAmount = 0.3f
    wm.updateViewLayout(container, p)
}