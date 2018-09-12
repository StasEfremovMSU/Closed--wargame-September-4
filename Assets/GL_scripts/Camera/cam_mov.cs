using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class cam_mov : MonoBehaviour {
    public float range = 5f, moveSpeed = 3f;
    public float turnSpeed = 40f;

	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void Update () {


        if (GameStatus.UnitControl == false)
        {
            if (Input.GetKey(KeyCode.UpArrow))
                transform.Translate(Vector3.forward * 4 * moveSpeed * Time.deltaTime);

            if (Input.GetKey(KeyCode.DownArrow))
                transform.Translate(-Vector3.forward * 4 * moveSpeed * Time.deltaTime);

            if (Input.GetKey(KeyCode.LeftArrow))
                transform.Rotate(Vector3.up, -turnSpeed * 4f * Time.deltaTime);

            if (Input.GetKey(KeyCode.RightArrow))
                transform.Rotate(Vector3.up, turnSpeed * 4f * Time.deltaTime);
        }
	}
}
