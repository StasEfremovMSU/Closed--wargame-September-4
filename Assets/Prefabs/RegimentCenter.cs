using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class RegimentCenter : MonoBehaviour {

    public int number;

	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void Update () {
        transform.position = mainStatic.regiment_stat[number].regPosition;
	}
}
