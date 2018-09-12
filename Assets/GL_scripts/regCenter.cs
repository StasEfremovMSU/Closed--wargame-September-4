using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class regCenter : MonoBehaviour {

    [SerializeField]
    int i;
	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void Update () {
        transform.position = new Vector3(mainStatic.regiment_stat[i].regPosition.x, 10.0f, mainStatic.regiment_stat[i].regPosition.z);
		
	}
}
