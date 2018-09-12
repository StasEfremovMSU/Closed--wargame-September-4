using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class my_object : MonoBehaviour {

    public bool rot;
	
	// Update is called once per frame
	void Update () {
        if (rot)
        {
            Rotation();
        }
    }


        void Rotation()
    {
        if (Input.GetKey(KeyCode.Q) == true)
        {
            transform.Rotate(new Vector3 (0,1,0), 3f);
        }
        if (Input.GetKey(KeyCode.E) == true)
        {
            transform.Rotate(new Vector3(0, 1, 0), -3f);
        }
        if (Input.GetKeyDown(KeyCode.R) == true)
        {
            rot = false;
           tbstatus.rotation = false;
        }
    }


}

