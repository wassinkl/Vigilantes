package edu.umich.Vigilantes

import android.content.Context
import android.net.Uri
import android.telecom.Call
import android.widget.TextView
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import java.io.IOException
import com.android.volley.toolbox.JsonObjectRequest
import com.android.volley.toolbox.Volley.newRequestQueue
import kotlin.reflect.full.declaredMemberProperties
import okhttp3.*
import okhttp3.RequestBody.Companion.asRequestBody
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import kotlin.reflect.full.declaredMemberProperties
import androidx.appcompat.app.AppCompatActivity
import java.lang.Exception
import java.util.concurrent.TimeUnit


object reportStoreLicense {

    private val serverUrl = "https://3.137.139.79/"

    private val client = OkHttpClient.Builder()
        .connectTimeout(20, TimeUnit.SECONDS)
        .writeTimeout(20, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .build()

    var lpn_predict = ""
    var state_predict = ""

    fun postImagesLicense(context: Context, imageUri: Uri?,
                   completion: (String) -> Unit?) {
        val mpFD = MultipartBody.Builder().setType(MultipartBody.FORM)
        imageUri?.run {
            toFile(context)?.let {
                mpFD.addFormDataPart("image", "carImage",
                    it.asRequestBody("image/jpeg".toMediaType()))
            } ?: context.toast("Unsupported image format")
        }

        val request = Request.Builder()
            .url(serverUrl+"postplates/")
            .post(mpFD.build())
            .build()

        context.toast("Posting . . .")

        client.newCall(request).enqueue(object : Callback {
            override fun onFailure(call: okhttp3.Call, e: IOException) {
                completion(e.localizedMessage ?: "Posting failed")
            }

            override fun onResponse(call: okhttp3.Call, response: Response) {
                if (response.isSuccessful) {
                    val result = try { JSONObject(response.body?.string() ?: "") } catch (e: JSONException) { return }
                    try{
                        lpn_predict = result["lpn"] as String
                    }
                    catch (e: Exception){

                    }
                    try{
                        state_predict = result["state"] as String
                    }
                    catch (e: Exception){

                    }
                    completion("Added license plate number")
                }
                else{
                    completion("Failed to extract license plate... Is certificate installed?")
                }
            }
        })

    }


}