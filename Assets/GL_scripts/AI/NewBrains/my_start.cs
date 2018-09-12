using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class my_start : MonoBehaviour {

	public GameObject pointpref;

	public GameObject linepref;

	// Use this for initialization
	void Start () {
		
	}
	
	// Update is called once per frame
	void MyInst (int com) {
		for (int i = 0; i < AI.Gen_stat[com].parts.Count; i++)
		{
			for (int d = 0; d < 4; d++) {
				AI.Gen_stat [com].parts [i].Bpoints [d] = Instantiate (pointpref, 
					new Vector3 (AI.Gen_stat [com].parts [i].points [d].x, 1, AI.Gen_stat [com].parts [i].points [d].y),
					Quaternion.identity
				);
			}
		}

		for (int i = 0; i < AI.Gen_stat[com].parts.Count; i++)
		{
			Instantiate (linepref, 
					new Vector3 (0, 1, 0),
					Quaternion.identity
				);
		}
	}
}
