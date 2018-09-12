using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class FarOficerBrains : MonoBehaviour {

    public Vector3 position;

    public bool platerControl;

    // Глобальный порядковый номер по которому будет искать координату к которой надо идти
    public int gl_number;

    public Vector3 new_position;
    public Vector3 matrix_position;

    public int command;

    public int k1;
    public int k2;

    public int numReg;
    public int numSoldier;

    public bool testofficer;

    public float angle;

    public float speed = 2;

    public float attack = 20;
    public float defence = 4;
    public float power = 1;

	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void Update () {
		
	}
}
