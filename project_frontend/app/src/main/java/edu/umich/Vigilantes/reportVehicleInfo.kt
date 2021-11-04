package edu.umich.Vigilantes

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.view.View
import android.view.ViewGroup
import android.widget.*
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import edu.umich.Vigilantes.databinding.ActivityMainBinding
import edu.umich.Vigilantes.databinding.ActivityReportParticipantBinding
import kotlinx.android.synthetic.main.activity_report_participant.*

class reportVehicleInfo : AppCompatActivity(), vehicleAdapter.OnItemClickListener {
    private val reportVehicles: MutableList<VehicleInfo> = mutableListOf()
    private val adapter = vehicleAdapter(reportVehicles, this)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_report_vehicle)

        recycler_view.adapter = adapter
        recycler_view.layoutManager = LinearLayoutManager(this)
        recycler_view.setHasFixedSize(true)

        val addVehicleButton = findViewById<Button>(R.id.addVehicleButton)

        addVehicleButton.setOnClickListener {
            val vehicle = VehicleInfo()
            val intent = Intent(this, addVehicleForm::class.java)
            intent.putExtra("Vehicle Info", vehicle)    //Parcelize participant info
            addVehicle.launch(intent)
        }
    }

    override fun onItemClick(position: Int) {
        val vehicle = reportVehicles[position]
        val intent = Intent(this, vehiclePreview::class.java)
        intent.putExtra("Existing Vehicle", vehicle)
        editVehicle.launch(intent)
    }

    private val addVehicle =
        registerForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) {
            if(it.resultCode == Activity.RESULT_OK) {
                //Retrieve ParticipantInfo object from call
                val vehicle = it.data?.getParcelableExtra<VehicleInfo>("Vehicle Info")
                Log.d("debug message", "Vehicle Info received")
                if (vehicle != null) {
                    vehicle.makemodel?.let { it1 -> Log.d("Vehicle added ", it1) }
                    vehicle.position = reportVehicles.size  //Update participant position in list
                    Log.d("vehicle", vehicle.makemodel + " is now at position " + vehicle.position)
                    reportVehicles.add(vehicle) //Add retrieved participant to list of participants
                    adapter.notifyItemInserted(reportVehicles.size) //Update last participant in list
                }
            }
            else {
                Log.d("debug message", "Vehicle Info lost")
            }
        }

    private val editVehicle =
        registerForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) {
            if (it.resultCode == Activity.RESULT_OK) {
                //Retrieve ParticipantInfo object from call
                val editedVehicle = it.data?.getParcelableExtra<VehicleInfo>("Edited Vehicle")
                Log.d("debug message", "participant edited successfully")
                if (editedVehicle != null) {
                    reportVehicles[editedVehicle.position!!] = editedVehicle    //Update participant at returned position
                    adapter.notifyItemChanged(editedVehicle.position!!) //Update list at returned position
                }
            }
            else if(it.resultCode == 66) {  //If participant is deleted
                //Retrieve ParticipantInfo object from call
                val deletedIndex = it.data?.getIntExtra("Delete index", -1)
                Log.d("debug message", "vehicle at position $deletedIndex deleted")
                if (deletedIndex != null) {
                    reportVehicles.removeAt(deletedIndex)
                    updatePositions(deletedIndex)
                    adapter.notifyItemRemoved(deletedIndex)
                    adapter.notifyItemRangeChanged(deletedIndex, reportVehicles.size)
                }
            }
            else if(it.resultCode == Activity.RESULT_CANCELED) {    //If changes are discarded
                Log.d("debug message", "vehicle edit changes discarded")
            }
            else {
                Log.d("debug message", "vehicle edit failed")
            }
        }

    //Used to update participant's positions in list after a deletion
    private fun updatePositions(position: Int) {
        for((index, vehicle) in reportVehicles.withIndex()) {
            if(index >= position) {
                vehicle.position = index
                Log.d("debug message", "vehicle ${vehicle.makemodel} is now at position ${vehicle.position}")
            }
        }
    }
}