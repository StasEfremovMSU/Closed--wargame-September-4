using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class tbstatus_upd : MonoBehaviour {

	
	// Update is called once per frame
	void Update () {
        if (Input.GetKeyDown(KeyCode.R))
        {
            if (tbstatus.rotation)
                tbstatus.rotation = false;
            else
                tbstatus.rotation = true;
        }
        if (Input.GetKeyDown(KeyCode.T))
        {
            tbstatus.mov = true;
        }

        if (tbstatus.rotation)
        {
            if (Input.GetMouseButtonDown(0))
            {
                RaycastHit hit;
                Ray rayh = Camera.main.ScreenPointToRay(Input.mousePosition);
                Physics.Raycast(rayh, out hit);
                if (hit.transform.gameObject.GetComponent<my_object>())
                {
                    hit.transform.gameObject.GetComponent<my_object>().rot = true;
                }
            }
        }

	}
}
