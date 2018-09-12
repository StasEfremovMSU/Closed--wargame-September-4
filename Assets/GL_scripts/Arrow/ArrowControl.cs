using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class ArrowControl : MonoBehaviour {

    public float speed;
    public Quaternion forw;

    public void put(float sp, Quaternion f)
    {
        speed = sp;
        forw = f;
    }

	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void Update () {
		transform.position = transform.position + transform.forward * speed * Time.deltaTime;
        if (transform.position.y < 1f)
            Destroy(gameObject);

        
	}
}
