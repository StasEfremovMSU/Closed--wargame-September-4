using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class PlalerCam : MonoBehaviour {

    public Camera main_cam;


	// Use this for initialization
	void Start () {
        main_cam.gameObject.SetActive(true);
        gameObject.SetActive(false);
	}

    void OnMouseDown()
    {
        

        main_cam.gameObject.SetActive(false);
        gameObject.SetActive(true);
    }

	// Update is called once per frame
	void Update () {
		
	}
}
